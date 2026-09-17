#include "stocks_toolkit/data_layer/bar_validator.h"

#include <gtest/gtest.h>

using stocks_toolkit::PriceBar;
using stocks_toolkit::validate_bars;

namespace {

PriceBar make_bar(std::string date, double open, double high, double low, double close,
                   int64_t volume = 1000) {
    PriceBar bar;
    bar.date = std::move(date);
    bar.open = open;
    bar.high = high;
    bar.low = low;
    bar.close = close;
    bar.adj_close = close;
    bar.volume = volume;
    return bar;
}

}  // namespace

TEST(BarValidator, KeepsSaneBars) {
    std::vector<PriceBar> bars = {make_bar("2024-01-02", 10, 11, 9, 10.5),
                                    make_bar("2024-01-03", 10.5, 12, 10, 11)};
    EXPECT_EQ(validate_bars("nvda", bars).size(), 2u);
}

TEST(BarValidator, DropsNonPositivePrice) {
    std::vector<PriceBar> bars = {make_bar("2024-01-02", 0, 11, 9, 10.5)};
    EXPECT_TRUE(validate_bars("nvda", bars).empty());
}

TEST(BarValidator, DropsHighLessThanLow) {
    std::vector<PriceBar> bars = {make_bar("2024-01-02", 10, 9, 11, 10)};
    EXPECT_TRUE(validate_bars("nvda", bars).empty());
}

TEST(BarValidator, DropsCloseOutsideLowHighRange) {
    std::vector<PriceBar> bars = {make_bar("2024-01-02", 10, 11, 9, 15)};
    EXPECT_TRUE(validate_bars("nvda", bars).empty());
}

TEST(BarValidator, DropsNegativeVolume) {
    PriceBar bar = make_bar("2024-01-02", 10, 11, 9, 10.5, -5);
    EXPECT_TRUE(validate_bars("nvda", {bar}).empty());
}

TEST(BarValidator, DropsOutOfOrderOrDuplicateDates) {
    std::vector<PriceBar> bars = {make_bar("2024-01-03", 10, 11, 9, 10.5),
                                    make_bar("2024-01-02", 10, 11, 9, 10.5),
                                    make_bar("2024-01-03", 10, 11, 9, 10.5)};
    auto valid = validate_bars("nvda", bars);
    ASSERT_EQ(valid.size(), 1u);
    EXPECT_EQ(valid[0].date, "2024-01-03");
}

TEST(BarValidator, OneBadBarDoesNotDropTheRest) {
    std::vector<PriceBar> bars = {make_bar("2024-01-02", 10, 11, 9, 10.5),
                                    make_bar("2024-01-03", -1, 11, 9, 10.5),
                                    make_bar("2024-01-04", 10, 11, 9, 10.5)};
    auto valid = validate_bars("nvda", bars);
    ASSERT_EQ(valid.size(), 2u);
    EXPECT_EQ(valid[0].date, "2024-01-02");
    EXPECT_EQ(valid[1].date, "2024-01-04");
}
