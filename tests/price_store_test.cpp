#include "stocks_toolkit/data_layer/price_store.h"

#include <sqlite3.h>

#include <cstdio>
#include <gtest/gtest.h>
#include <stdexcept>

using stocks_toolkit::PriceBar;
using stocks_toolkit::PriceStore;

namespace {

PriceBar make_bar(std::string date, double close) {
    PriceBar bar;
    bar.date = std::move(date);
    bar.open = bar.high = bar.low = bar.close = close;
    bar.volume = 1000;
    return bar;
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
