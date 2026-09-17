#include "stocks_toolkit/data_layer/price_store.h"

#include <sqlite3.h>

#include <stdexcept>

namespace stocks_toolkit {

namespace {

void check(sqlite3* db, int rc, const char* what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string(what) + ": " + sqlite3_errmsg(db));
    }
}

struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() { sqlite3_finalize(stmt); }
};

// RAII transaction: BEGINs on construction, ROLLBACKs on destruction unless
// commit() was called first. This guarantees a failure partway through a
// batch of writes (e.g. SQLITE_BUSY, disk I/O error) can't leave the
// connection stuck inside an open transaction, which would otherwise make
// every subsequent write fail with "cannot start a transaction within a
// transaction".
struct TransactionGuard {
    sqlite3* db;
    bool active = false;

    explicit TransactionGuard(sqlite3* db_) : db(db_) {
        check(db, sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr), "begin");
        active = true;
    }

    void commit() {
        check(db, sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr), "commit");
        active = false;
    }

    ~TransactionGuard() {
        if (active) {
            // Best-effort: we're already unwinding from a failure, so there's no
            // good way to react to a rollback error here.
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
};

// CREATE TABLE IF NOT EXISTS is a no-op against a `prices` table that already
// exists under an older schema — it silently keeps the old columns instead of
// adding adj_close, and every subsequent upsert_bars() then fails per-ticker
// with a cryptic "no column named adj_close" instead of a clear diagnosis.
bool prices_table_has_adj_close_column(sqlite3* db) {
    StmtGuard guard;
    check(db, sqlite3_prepare_v2(db, "PRAGMA table_info(prices);", -1, &guard.stmt, nullptr),
          "prepare table_info(prices)");
    while (true) {
        int rc = sqlite3_step(guard.stmt);
        if (rc == SQLITE_DONE) return false;
        check(db, rc, "step table_info(prices)");
        const unsigned char* name = sqlite3_column_text(guard.stmt, 1);  // column 1 = name
        if (name && std::string(reinterpret_cast<const char*>(name)) == "adj_close") return true;
    }
}

const char* to_text(CorporateActionType type) {
    return type == CorporateActionType::kDividend ? "dividend" : "split";
}

CorporateActionType action_type_from_text(const unsigned char* text) {
    return std::string(reinterpret_cast<const char*>(text)) == "dividend"
               ? CorporateActionType::kDividend
               : CorporateActionType::kSplit;
}

}  // namespace

PriceStore::PriceStore(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) sqlite3_close(db_);
        throw std::runtime_error("failed to open database '" + db_path + "': " + msg);
    }

    const char* schema =
        "CREATE TABLE IF NOT EXISTS prices ("
        "  ticker TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  open REAL NOT NULL,"
        "  high REAL NOT NULL,"
        "  low REAL NOT NULL,"
        "  close REAL NOT NULL,"
        "  adj_close REAL NOT NULL,"
        "  volume INTEGER NOT NULL,"
        "  PRIMARY KEY (ticker, date)"
        ");"
        "CREATE TABLE IF NOT EXISTS corporate_actions ("
        "  ticker TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  type TEXT NOT NULL CHECK(type IN ('dividend', 'split')),"
        "  amount REAL,"
        "  split_numerator REAL,"
        "  split_denominator REAL,"
        "  PRIMARY KEY (ticker, date, type)"
        ");"
        "CREATE TABLE IF NOT EXISTS ticker_metadata ("
        "  ticker TEXT PRIMARY KEY,"
        "  is_benchmark INTEGER NOT NULL DEFAULT 0"
        ");";
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        sqlite3_close(db_);
        throw std::runtime_error("failed to create schema: " + msg);
    }

    if (!prices_table_has_adj_close_column(db_)) {
        sqlite3_close(db_);
        throw std::runtime_error(
            "database '" + db_path +
            "' has an outdated 'prices' schema (missing adj_close) from before the M0 schema "
            "update — this toolkit does not migrate schemas, delete the file and re-fetch");
    }
}

PriceStore::~PriceStore() {
    if (db_) sqlite3_close(db_);
}

void PriceStore::upsert_bars(const std::string& ticker, const std::vector<PriceBar>& bars) {
    if (bars.empty()) return;

    TransactionGuard txn(db_);

    const char* sql =
        "INSERT INTO prices (ticker, date, open, high, low, close, adj_close, volume) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(ticker, date) DO UPDATE SET "
        "  open=excluded.open, high=excluded.high, low=excluded.low, "
        "  close=excluded.close, adj_close=excluded.adj_close, volume=excluded.volume;";

    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare upsert");

    for (const auto& bar : bars) {
        sqlite3_reset(guard.stmt);
        sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(guard.stmt, 2, bar.date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(guard.stmt, 3, bar.open);
        sqlite3_bind_double(guard.stmt, 4, bar.high);
        sqlite3_bind_double(guard.stmt, 5, bar.low);
        sqlite3_bind_double(guard.stmt, 6, bar.close);
        sqlite3_bind_double(guard.stmt, 7, bar.adj_close);
        sqlite3_bind_int64(guard.stmt, 8, bar.volume);
        check(db_, sqlite3_step(guard.stmt), "step upsert");
    }

    txn.commit();
}

std::optional<std::string> PriceStore::last_date(const std::string& ticker) {
    const char* sql = "SELECT MAX(date) FROM prices WHERE ticker = ?;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare last_date");
    sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::string> result;
    int rc = sqlite3_step(guard.stmt);
    check(db_, rc, "step last_date");
    if (rc == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(guard.stmt, 0);
        if (text) result = std::string(reinterpret_cast<const char*>(text));
    }
    return result;
}

std::vector<PriceBar> PriceStore::get_bars(const std::string& ticker,
                                            const std::string& start_date,
                                            const std::string& end_date) {
    const char* sql =
        "SELECT date, open, high, low, close, adj_close, volume FROM prices "
        "WHERE ticker = ? AND date BETWEEN ? AND ? ORDER BY date;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare get_bars");
    sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(guard.stmt, 2, start_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(guard.stmt, 3, end_date.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<PriceBar> bars;
    while (true) {
        int rc = sqlite3_step(guard.stmt);
        if (rc == SQLITE_DONE) break;
        check(db_, rc, "step get_bars");

        const unsigned char* date_text = sqlite3_column_text(guard.stmt, 0);
        if (!date_text) {
            // Schema declares date TEXT NOT NULL, so this indicates DB corruption
            // or a schema change — fail loudly rather than construct a string
            // from a null pointer.
            throw std::runtime_error("get_bars: NULL date column despite NOT NULL constraint");
        }

        PriceBar bar;
        bar.date = reinterpret_cast<const char*>(date_text);
        bar.open = sqlite3_column_double(guard.stmt, 1);
        bar.high = sqlite3_column_double(guard.stmt, 2);
        bar.low = sqlite3_column_double(guard.stmt, 3);
        bar.close = sqlite3_column_double(guard.stmt, 4);
        bar.adj_close = sqlite3_column_double(guard.stmt, 5);
        bar.volume = sqlite3_column_int64(guard.stmt, 6);
        bars.push_back(std::move(bar));
    }
    return bars;
}

std::vector<PriceBar> PriceStore::get_last_n_bars(const std::string& ticker,
                                                    const std::string& as_of_date, int n) {
    if (n <= 0) {
        throw std::invalid_argument("get_last_n_bars: n must be positive");
    }

    const char* sql =
        "SELECT date, open, high, low, close, adj_close, volume FROM ("
        "  SELECT date, open, high, low, close, adj_close, volume FROM prices"
        "  WHERE ticker = ? AND date <= ? ORDER BY date DESC LIMIT ?"
        ") ORDER BY date ASC;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare get_last_n_bars");
    sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(guard.stmt, 2, as_of_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(guard.stmt, 3, n);

    std::vector<PriceBar> bars;
    while (true) {
        int rc = sqlite3_step(guard.stmt);
        if (rc == SQLITE_DONE) break;
        check(db_, rc, "step get_last_n_bars");

        const unsigned char* date_text = sqlite3_column_text(guard.stmt, 0);
        if (!date_text) {
            throw std::runtime_error("get_last_n_bars: NULL date column despite NOT NULL constraint");
        }

        PriceBar bar;
        bar.date = reinterpret_cast<const char*>(date_text);
        bar.open = sqlite3_column_double(guard.stmt, 1);
        bar.high = sqlite3_column_double(guard.stmt, 2);
        bar.low = sqlite3_column_double(guard.stmt, 3);
        bar.close = sqlite3_column_double(guard.stmt, 4);
        bar.adj_close = sqlite3_column_double(guard.stmt, 5);
        bar.volume = sqlite3_column_int64(guard.stmt, 6);
        bars.push_back(std::move(bar));
    }
    return bars;
}

void PriceStore::upsert_corporate_actions(const std::string& ticker,
                                           const std::vector<CorporateAction>& actions) {
    if (actions.empty()) return;

    TransactionGuard txn(db_);

    const char* sql =
        "INSERT INTO corporate_actions "
        "  (ticker, date, type, amount, split_numerator, split_denominator) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(ticker, date, type) DO UPDATE SET "
        "  amount=excluded.amount, split_numerator=excluded.split_numerator, "
        "  split_denominator=excluded.split_denominator;";

    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr),
          "prepare upsert_corporate_actions");

    for (const auto& action : actions) {
        sqlite3_reset(guard.stmt);
        sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(guard.stmt, 2, action.date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(guard.stmt, 3, to_text(action.type), -1, SQLITE_STATIC);
        sqlite3_bind_double(guard.stmt, 4, action.amount);
        sqlite3_bind_double(guard.stmt, 5, action.split_numerator);
        sqlite3_bind_double(guard.stmt, 6, action.split_denominator);
        check(db_, sqlite3_step(guard.stmt), "step upsert_corporate_actions");
    }

    txn.commit();
}

std::vector<CorporateAction> PriceStore::get_corporate_actions(const std::string& ticker,
                                                                 const std::string& start_date,
                                                                 const std::string& end_date) {
    const char* sql =
        "SELECT date, type, amount, split_numerator, split_denominator FROM corporate_actions "
        "WHERE ticker = ? AND date BETWEEN ? AND ? ORDER BY date;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr),
          "prepare get_corporate_actions");
    sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(guard.stmt, 2, start_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(guard.stmt, 3, end_date.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<CorporateAction> actions;
    while (true) {
        int rc = sqlite3_step(guard.stmt);
        if (rc == SQLITE_DONE) break;
        check(db_, rc, "step get_corporate_actions");

        CorporateAction action;
        action.date = reinterpret_cast<const char*>(sqlite3_column_text(guard.stmt, 0));
        action.type = action_type_from_text(sqlite3_column_text(guard.stmt, 1));
        action.amount = sqlite3_column_double(guard.stmt, 2);
        action.split_numerator = sqlite3_column_double(guard.stmt, 3);
        action.split_denominator = sqlite3_column_double(guard.stmt, 4);
        actions.push_back(std::move(action));
    }
    return actions;
}

void PriceStore::set_is_benchmark(const std::string& ticker, bool is_benchmark) {
    const char* sql =
        "INSERT INTO ticker_metadata (ticker, is_benchmark) VALUES (?, ?) "
        "ON CONFLICT(ticker) DO UPDATE SET is_benchmark=excluded.is_benchmark;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare set_is_benchmark");
    sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(guard.stmt, 2, is_benchmark ? 1 : 0);
    check(db_, sqlite3_step(guard.stmt), "step set_is_benchmark");
}

bool PriceStore::is_benchmark(const std::string& ticker) {
    const char* sql = "SELECT is_benchmark FROM ticker_metadata WHERE ticker = ?;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare is_benchmark");
    sqlite3_bind_text(guard.stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(guard.stmt);
    check(db_, rc, "step is_benchmark");
    return rc == SQLITE_ROW && sqlite3_column_int(guard.stmt, 0) != 0;
}

std::vector<std::string> PriceStore::benchmark_tickers() {
    const char* sql = "SELECT ticker FROM ticker_metadata WHERE is_benchmark != 0;";
    StmtGuard guard;
    check(db_, sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), "prepare benchmark_tickers");

    std::vector<std::string> tickers;
    while (true) {
        int rc = sqlite3_step(guard.stmt);
        if (rc == SQLITE_DONE) break;
        check(db_, rc, "step benchmark_tickers");
        tickers.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(guard.stmt, 0)));
    }
    return tickers;
}

}  // namespace stocks_toolkit
