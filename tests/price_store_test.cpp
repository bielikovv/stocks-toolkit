#include "stocks_toolkit/data_layer/price_store.h"

#include <sqlite3.h>

#include <cstdio>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>

using stocks_toolkit::CorporateAction;
using stocks_toolkit::CorporateActionType;
using stocks_toolkit::PriceBar;
using stocks_toolkit::PriceStore;

namespace {

PriceBar make_bar(std::string date, double close) {
    PriceBar bar;
    bar.date = std::move(date);
    bar.open = bar.high = bar.low = bar.close = bar.adj_close = close;
    bar.volume = 1000;
    return bar;
}

CorporateAction make_dividend(std::string date, double amount) {
    CorporateAction action;
    action.date = std::move(date);
    action.type = CorporateActionType::kDividend;
    action.amount = amount;
    return action;
}

CorporateAction make_split(std::string date, double numerator, double denominator) {
    CorporateAction action;
    action.date = std::move(date);
    action.type = CorporateActionType::kSplit;
    action.split_numerator = numerator;
    action.split_denominator = denominator;
    return action;
}

}  // namespace

TEST(PriceStore, LastDateIsNulloptWhenEmpty) {
    PriceStore store(":memory:");
    EXPECT_FALSE(store.last_date("nvda.us").has_value());
}

TEST(PriceStore, UpsertThenLastDateReturnsMostRecent) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0), make_bar("2024-01-03", 11.0)});

    auto last = store.last_date("nvda.us");
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(*last, "2024-01-03");
}

TEST(PriceStore, UpsertIsIdempotentOnTickerAndDate) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0)});
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 99.0)});  // same date, new value

    auto bars = store.get_bars("nvda.us", "2024-01-01", "2024-01-31");
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 99.0);
}

TEST(PriceStore, GetBarsFiltersByDateRangeAndOrders) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-05", 1.0), make_bar("2024-01-02", 2.0),
                                  make_bar("2024-01-10", 3.0)});

    auto bars = store.get_bars("nvda.us", "2024-01-01", "2024-01-06");

    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2024-01-02");
    EXPECT_EQ(bars[1].date, "2024-01-05");
}

TEST(PriceStore, TickersDoNotCrossContaminate) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0)});
    store.upsert_bars("tsla.us", {make_bar("2024-01-02", 20.0), make_bar("2024-01-03", 21.0)});

    EXPECT_EQ(store.get_bars("nvda.us", "2024-01-01", "2024-01-31").size(), 1u);
    EXPECT_EQ(store.get_bars("tsla.us", "2024-01-01", "2024-01-31").size(), 2u);
    EXPECT_EQ(*store.last_date("tsla.us"), "2024-01-03");
}

TEST(PriceStore, GetLastNBarsIsEmptyWhenNoData) {
    PriceStore store(":memory:");
    EXPECT_TRUE(store.get_last_n_bars("nvda.us", "2024-01-31", 5).empty());
}

TEST(PriceStore, GetLastNBarsReturnsFewerThanNWhenNotEnoughHistory) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0), make_bar("2024-01-03", 11.0)});

    auto bars = store.get_last_n_bars("nvda.us", "2024-01-31", 5);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2024-01-02");
    EXPECT_EQ(bars[1].date, "2024-01-03");
}

TEST(PriceStore, GetLastNBarsReturnsMostRecentNOrderedAscending) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 1.0), make_bar("2024-01-03", 2.0),
                                  make_bar("2024-01-04", 3.0), make_bar("2024-01-05", 4.0)});

    auto bars = store.get_last_n_bars("nvda.us", "2024-01-05", 2);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2024-01-04");
    EXPECT_EQ(bars[1].date, "2024-01-05");
}

TEST(PriceStore, GetLastNBarsRespectsAsOfDateCutoff) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 1.0), make_bar("2024-01-03", 2.0),
                                  make_bar("2024-01-04", 3.0)});

    auto bars = store.get_last_n_bars("nvda.us", "2024-01-03", 5);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2024-01-02");
    EXPECT_EQ(bars[1].date, "2024-01-03");
}

TEST(PriceStore, GetLastNBarsThrowsWhenNIsZero) {
    PriceStore store(":memory:");
    EXPECT_THROW(store.get_last_n_bars("nvda.us", "2024-01-31", 0), std::invalid_argument);
}

TEST(PriceStore, GetLastNBarsThrowsWhenNIsNegative) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0)});
    EXPECT_THROW(store.get_last_n_bars("nvda.us", "2024-01-31", -1), std::invalid_argument);
}

TEST(PriceStore, GetLastNBarsDoesNotCrossContaminateTickers) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", {make_bar("2024-01-02", 1.0)});
    store.upsert_bars("tsla.us", {make_bar("2024-01-02", 2.0), make_bar("2024-01-03", 3.0)});

    EXPECT_EQ(store.get_last_n_bars("nvda.us", "2024-01-31", 5).size(), 1u);
    EXPECT_EQ(store.get_last_n_bars("tsla.us", "2024-01-31", 5).size(), 2u);
}

TEST(PriceStore, RoundTripsAdjClose) {
    PriceStore store(":memory:");
    PriceBar bar = make_bar("2024-01-02", 10.0);
    bar.adj_close = 9.5;  // diverges from raw close, e.g. after a dividend
    store.upsert_bars("nvda.us", {bar});

    auto bars = store.get_bars("nvda.us", "2024-01-01", "2024-01-31");
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 10.0);
    EXPECT_DOUBLE_EQ(bars[0].adj_close, 9.5);
}

TEST(PriceStore, UpsertCorporateActionsThenGetReturnsThemInDateOrder) {
    PriceStore store(":memory:");
    store.upsert_corporate_actions(
        "aapl.us", {make_split("2024-06-10", 10.0, 1.0), make_dividend("2024-01-05", 0.24)});

    auto actions = store.get_corporate_actions("aapl.us", "2024-01-01", "2024-12-31");
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].date, "2024-01-05");
    EXPECT_EQ(actions[0].type, CorporateActionType::kDividend);
    EXPECT_DOUBLE_EQ(actions[0].amount, 0.24);
    EXPECT_EQ(actions[1].date, "2024-06-10");
    EXPECT_EQ(actions[1].type, CorporateActionType::kSplit);
    EXPECT_DOUBLE_EQ(actions[1].split_numerator, 10.0);
}

TEST(PriceStore, UpsertCorporateActionsIsIdempotentOnTickerDateAndType) {
    PriceStore store(":memory:");
    store.upsert_corporate_actions("aapl.us", {make_dividend("2024-01-05", 0.24)});
    store.upsert_corporate_actions("aapl.us", {make_dividend("2024-01-05", 0.30)});  // corrected

    auto actions = store.get_corporate_actions("aapl.us", "2024-01-01", "2024-12-31");
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_DOUBLE_EQ(actions[0].amount, 0.30);
}

TEST(PriceStore, DividendAndSplitOnSameDateBothStored) {
    PriceStore store(":memory:");
    store.upsert_corporate_actions(
        "aapl.us", {make_dividend("2024-06-10", 0.24), make_split("2024-06-10", 4.0, 1.0)});

    EXPECT_EQ(store.get_corporate_actions("aapl.us", "2024-01-01", "2024-12-31").size(), 2u);
}

TEST(PriceStore, IsBenchmarkDefaultsFalse) {
    PriceStore store(":memory:");
    EXPECT_FALSE(store.is_benchmark("spy"));
}

TEST(PriceStore, SetIsBenchmarkRoundTrips) {
    PriceStore store(":memory:");
    store.set_is_benchmark("spy", true);
    store.set_is_benchmark("nvda", false);

    EXPECT_TRUE(store.is_benchmark("spy"));
    EXPECT_FALSE(store.is_benchmark("nvda"));
}

TEST(PriceStore, SetIsBenchmarkCanBeUpdated) {
    PriceStore store(":memory:");
    store.set_is_benchmark("spy", true);
    store.set_is_benchmark("spy", false);

    EXPECT_FALSE(store.is_benchmark("spy"));
}

TEST(PriceStore, BenchmarkTickersListsOnlyFlaggedOnes) {
    PriceStore store(":memory:");
    store.set_is_benchmark("spy", true);
    store.set_is_benchmark("qqq", true);
    store.set_is_benchmark("nvda", false);

    auto benchmarks = store.benchmark_tickers();
    EXPECT_EQ(benchmarks.size(), 2u);
    EXPECT_THAT(benchmarks, ::testing::UnorderedElementsAre("spy", "qqq"));
}

TEST(PriceStore, RejectsOutdatedSchemaMissingAdjClose) {
    // Regression test: CREATE TABLE IF NOT EXISTS is a no-op against a `prices`
    // table that already exists under the pre-adj_close schema, which used to
    // make every upsert_bars() fail per-ticker with a cryptic "no column named
    // adj_close" instead of a clear, actionable error at open time.
    std::string path = "price_store_test_outdated_schema.db";
    std::remove(path.c_str());

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
                            "CREATE TABLE prices ("
                            "  ticker TEXT NOT NULL, date TEXT NOT NULL, open REAL NOT NULL,"
                            "  high REAL NOT NULL, low REAL NOT NULL, close REAL NOT NULL,"
                            "  volume INTEGER NOT NULL, PRIMARY KEY (ticker, date));",
                            nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);

    EXPECT_THROW(PriceStore store(path), std::runtime_error);

    std::remove(path.c_str());
}

TEST(PriceStore, RecoversAfterMidTransactionFailure) {
    // Regression test: upsert_bars() used to leave the connection stuck
    // inside an open transaction if a write failed partway through (no
    // ROLLBACK on the exception path) — every later upsert_bars() call would
    // then fail with "cannot start a transaction within a transaction".
    // Force a real write failure with a lock held by a second connection to
    // the same file (requires an on-disk file — ":memory:" isn't shared
    // across connections), then verify the store recovers afterward.
    std::string path = "price_store_test_recovery.db";
    std::remove(path.c_str());
    PriceStore store(path);

    sqlite3* blocker = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &blocker), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(blocker, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr), SQLITE_OK);

    EXPECT_THROW(store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0)}), std::runtime_error);

    sqlite3_exec(blocker, "ROLLBACK;", nullptr, nullptr, nullptr);
    sqlite3_close(blocker);

    EXPECT_NO_THROW(store.upsert_bars("nvda.us", {make_bar("2024-01-02", 10.0)}));

    std::remove(path.c_str());
}
