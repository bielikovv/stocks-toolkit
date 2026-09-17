#pragma once

#include <optional>
#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/corporate_action.h"
#include "stocks_toolkit/data_layer/price_bar.h"

struct sqlite3;

namespace stocks_toolkit {

// SQLite-backed storage for historical price bars, corporate actions, and
// per-ticker metadata. Owns one connection to a single database file. This
// is the query interface later modules (regime detection, backtester, etc.)
// are expected to depend on directly — they should never need to know about
// Yahoo or JSON parsing.
class PriceStore {
public:
    explicit PriceStore(const std::string& db_path);
    ~PriceStore();

    PriceStore(const PriceStore&) = delete;
    PriceStore& operator=(const PriceStore&) = delete;

    // Inserts or updates bars for `ticker`. Safe to call repeatedly with
    // overlapping data — keyed on (ticker, date).
    void upsert_bars(const std::string& ticker, const std::vector<PriceBar>& bars);

    // Most recent date stored for `ticker`, or nullopt if none stored yet.
    std::optional<std::string> last_date(const std::string& ticker);

    // Bars for `ticker` with date in [start_date, end_date], ordered by date.
    std::vector<PriceBar> get_bars(const std::string& ticker, const std::string& start_date,
                                    const std::string& end_date);

    // Up to the `n` most recent bars for `ticker` with date <= `as_of_date`, ordered by
    // date ascending (oldest first). Returns fewer than `n` if less history is stored.
    // Throws std::invalid_argument if `n` is not positive.
    std::vector<PriceBar> get_last_n_bars(const std::string& ticker, const std::string& as_of_date,
                                           int n);

    // Inserts or updates corporate actions for `ticker`. Safe to call
    // repeatedly with overlapping data — keyed on (ticker, date, type).
    void upsert_corporate_actions(const std::string& ticker,
                                   const std::vector<CorporateAction>& actions);

    // Corporate actions for `ticker` with date in [start_date, end_date], ordered by date.
    std::vector<CorporateAction> get_corporate_actions(const std::string& ticker,
                                                        const std::string& start_date,
                                                        const std::string& end_date);

    // Marks whether `ticker` is a benchmark (e.g. a market index like SPY)
    // rather than an asset under analysis. Later modules (relative strength,
    // regime classification) use this to find "the market" without a
    // hardcoded ticker name.
    void set_is_benchmark(const std::string& ticker, bool is_benchmark);

    // False for any ticker never marked via set_is_benchmark.
    bool is_benchmark(const std::string& ticker);

    // All tickers ever marked as a benchmark, in no particular order.
    std::vector<std::string> benchmark_tickers();

private:
    sqlite3* db_ = nullptr;
};

}  // namespace stocks_toolkit
