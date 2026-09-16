#include "stocks_toolkit/grid_calculator/grid_calculator.h"

#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/price_store.h"

using stocks_toolkit::calculate_grid_params;
using stocks_toolkit::GridCalculationStatus;
using stocks_toolkit::GridConfig;
using stocks_toolkit::GridParameterCalculator;
using stocks_toolkit::PriceBar;
using stocks_toolkit::PriceStore;

namespace {

PriceBar make_bar(std::string date, double open, double high, double low, double close) {
    PriceBar bar;
    bar.date = std::move(date);
    bar.open = open;
    bar.high = high;
    bar.low = low;
    bar.close = close;
    bar.volume = 1000;
    return bar;
}

// Flat OHLC bars (no intrabar range) whose closes step through `closes` —
// enough to exercise ATR's "gap between days" term without an intrabar
// range term muddying the numbers.
std::vector<PriceBar> make_flat_bars(std::vector<double> closes) {
    std::vector<PriceBar> bars;
    int day = 1;
    for (double close : closes) {
        char date[11];
        std::snprintf(date, sizeof(date), "2024-02-%02d", day++);
        bars.push_back(make_bar(date, close, close, close, close));
    }
    return bars;
}

}  // namespace

TEST(CalculateGridParams, InsufficientDataWhenFewerBarsThanAtrWindowPlusOne) {
    GridConfig config;
    config.atr_window_bars = 14;
    auto bars = make_flat_bars({100, 101, 102});

    EXPECT_EQ(calculate_grid_params(bars, config).status, GridCalculationStatus::kInsufficientData);
}

TEST(CalculateGridParams, ExactlyAtrWindowPlusOneBarsIsSufficient) {
    GridConfig config;
    config.atr_window_bars = 2;
    auto bars = make_flat_bars({100, 101, 102});

    EXPECT_NE(calculate_grid_params(bars, config).status, GridCalculationStatus::kInsufficientData);
}

TEST(CalculateGridParams, ThrowsWhenAtrWindowBarsLessThanOne) {
    GridConfig config;
    config.atr_window_bars = 0;

    EXPECT_THROW(calculate_grid_params({}, config), std::invalid_argument);
}

TEST(CalculateGridParams, ThrowsWhenNumLevelsLessThanTwo) {
    GridConfig config;
    config.num_levels = 1;

    EXPECT_THROW(calculate_grid_params({}, config), std::invalid_argument);
}

TEST(CalculateGridParams, ThrowsWhenTotalCapitalIsNegative) {
    GridConfig config;
    config.total_capital = -1.0;

    EXPECT_THROW(calculate_grid_params({}, config), std::invalid_argument);
}

TEST(CalculateGridParams, ThrowsWhenRangeAtrMultiplierIsNotPositive) {
    GridConfig config;
    config.range_atr_multiplier = 0.0;

    EXPECT_THROW(calculate_grid_params({}, config), std::invalid_argument);
}

TEST(CalculateGridParams, ThrowsWhenAtrWindowBarsExceedsMaximum) {
    GridConfig config;
    config.atr_window_bars = std::numeric_limits<int>::max();

    // Must throw rather than overflow `atr_window_bars + 1` internally.
    EXPECT_THROW(calculate_grid_params({}, config), std::invalid_argument);
}

TEST(CalculateGridParams, ComputesAverageTrueRangeAcrossWindow) {
    GridConfig config;
    config.atr_window_bars = 2;
    std::vector<PriceBar> bars = {
        make_bar("2024-02-01", 100, 100, 100, 100),
        // True range vs prev close 100: max(110-95=15, |110-100|=10, |95-100|=5) = 15
        make_bar("2024-02-02", 100, 110, 95, 105),
        // True range vs prev close 105: max(112-108=4, |112-105|=7, |108-105|=3) = 7
        make_bar("2024-02-03", 105, 112, 108, 109),
    };

    auto result = calculate_grid_params(bars, config);
    EXPECT_DOUBLE_EQ(result.atr, (15.0 + 7.0) / 2.0);
    EXPECT_DOUBLE_EQ(result.current_price, 109.0);
}

TEST(CalculateGridParams, RangeIsCurrentPricePlusMinusMultiplierTimesAtr) {
    GridConfig config;
    config.atr_window_bars = 1;
    config.range_atr_multiplier = 2.0;
    std::vector<PriceBar> bars = {
        make_bar("2024-02-01", 100, 100, 100, 100),
        make_bar("2024-02-02", 100, 110, 95, 105),  // ATR = 15 (single bar)
    };

    auto result = calculate_grid_params(bars, config);
    ASSERT_EQ(result.status, GridCalculationStatus::kOk);
    EXPECT_DOUBLE_EQ(result.atr, 15.0);
    EXPECT_DOUBLE_EQ(result.lower_bound, 105.0 - 2.0 * 15.0);
    EXPECT_DOUBLE_EQ(result.upper_bound, 105.0 + 2.0 * 15.0);
}

TEST(CalculateGridParams, LevelsAreGeometricallySpacedAndSpanTheFullRange) {
    GridConfig config;
    config.atr_window_bars = 1;
    config.num_levels = 5;
    std::vector<PriceBar> bars = {
        make_bar("2024-02-01", 100, 100, 100, 100),
        make_bar("2024-02-02", 100, 110, 95, 105),
    };

    auto result = calculate_grid_params(bars, config);
    ASSERT_EQ(result.status, GridCalculationStatus::kOk);
    ASSERT_EQ(result.levels.size(), 5u);
    EXPECT_NEAR(result.levels.front().price, result.lower_bound, 1e-9);
    EXPECT_NEAR(result.levels.back().price, result.upper_bound, 1e-9);

    double expected_ratio = 1.0 + result.spacing_pct;
    for (std::size_t i = 1; i < result.levels.size(); ++i) {
        EXPECT_NEAR(result.levels[i].price / result.levels[i - 1].price, expected_ratio, 1e-9);
    }
}

TEST(CalculateGridParams, CapitalAndQuantitySplitEvenlyAcrossLevels) {
    GridConfig config;
    config.atr_window_bars = 1;
    config.num_levels = 4;
    config.total_capital = 1000.0;
    std::vector<PriceBar> bars = {
        make_bar("2024-02-01", 100, 100, 100, 100),
        make_bar("2024-02-02", 100, 110, 95, 105),
    };

    auto result = calculate_grid_params(bars, config);
    ASSERT_EQ(result.status, GridCalculationStatus::kOk);
    ASSERT_EQ(result.levels.size(), 4u);
    for (const auto& level : result.levels) {
        EXPECT_DOUBLE_EQ(level.capital_allocated, 250.0);
        EXPECT_DOUBLE_EQ(level.quantity, 250.0 / level.price);
    }
}

TEST(CalculateGridParams, ZeroAtrIsInvalidVolatility) {
    GridConfig config;
    config.atr_window_bars = 2;
    // Perfectly flat series (e.g. a halted ticker, or a provider
    // forward-filling a holiday with the previous close) => ATR == 0.
    // Must not report kOk with every level stacked on one price.
    auto bars = make_flat_bars({100, 100, 100});

    auto result = calculate_grid_params(bars, config);
    EXPECT_DOUBLE_EQ(result.atr, 0.0);
    EXPECT_EQ(result.status, GridCalculationStatus::kInvalidVolatility);
    EXPECT_TRUE(result.levels.empty());
}

TEST(CalculateGridParams, InvalidRangeWhenLowerBoundBelowMinimumFraction) {
    GridConfig config;
    config.atr_window_bars = 1;
    config.range_atr_multiplier = 2.0;
    std::vector<PriceBar> bars = {
        make_bar("2024-02-01", 100, 100, 100, 100),
        // ATR = max(149.75-100, |149.75-100|, |100-100|) = 49.75
        // lower_bound = 100 - 2*49.75 = 0.5 -> positive, but only 0.5% of
        // current_price (100), below the 1% sanity floor.
        make_bar("2024-02-02", 100, 149.75, 100, 100),
    };

    auto result = calculate_grid_params(bars, config);
    EXPECT_GT(result.lower_bound, 0.0);
    EXPECT_EQ(result.status, GridCalculationStatus::kInvalidRange);
    EXPECT_TRUE(result.levels.empty());
}

TEST(CalculateGridParams, InvalidRangeWhenLowerBoundWouldBeNonPositive) {
    GridConfig config;
    config.atr_window_bars = 1;
    config.range_atr_multiplier = 5.0;
    std::vector<PriceBar> bars = {
        make_bar("2024-02-01", 10, 10, 10, 10),
        // ATR = max(15-5, |15-10|, |5-10|) = 10; current_price=10, lower = 10 - 5*10 = -40
        make_bar("2024-02-02", 10, 15, 5, 10),
    };

    auto result = calculate_grid_params(bars, config);
    EXPECT_EQ(result.status, GridCalculationStatus::kInvalidRange);
    EXPECT_TRUE(result.levels.empty());
}

TEST(GridParameterCalculatorClass, ConstructorThrowsImmediatelyOnInvalidConfig) {
    PriceStore store(":memory:");
    GridConfig config;
    config.atr_window_bars = -1;

    // Must fail at construction with a clear GridConfig message, not later
    // inside calculate() via a confusing PriceStore-level error.
    EXPECT_THROW(GridParameterCalculator(store, config), std::invalid_argument);
}

TEST(GridParameterCalculatorClass, DelegatesToStoreWindowAndCalculates) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", make_flat_bars({100, 101, 102, 103, 104}));

    GridConfig config;
    config.atr_window_bars = 4;
    GridParameterCalculator calculator(store, config);

    auto result = calculator.calculate("nvda.us", "2024-02-05");
    EXPECT_EQ(result.status, GridCalculationStatus::kOk);
    EXPECT_DOUBLE_EQ(result.current_price, 104.0);
}

TEST(GridParameterCalculatorClass, InsufficientDataWhenTickerHasNoHistory) {
    PriceStore store(":memory:");
    GridParameterCalculator calculator(store, GridConfig{});

    EXPECT_EQ(calculator.calculate("unknown.us", "2024-02-05").status,
              GridCalculationStatus::kInsufficientData);
}

TEST(GridParameterCalculatorClass, RespectsAsOfDateCutoff) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", make_flat_bars({100, 101, 102, 103, 104}));

    GridConfig config;
    config.atr_window_bars = 2;
    GridParameterCalculator calculator(store, config);

    auto result = calculator.calculate("nvda.us", "2024-02-03");
    ASSERT_EQ(result.status, GridCalculationStatus::kOk);
    EXPECT_DOUBLE_EQ(result.current_price, 102.0);  // day 3's close, later bars must not leak in
}
