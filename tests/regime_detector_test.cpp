#include "stocks_toolkit/regime_detector/regime_detector.h"

#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/price_store.h"

using stocks_toolkit::classify_regime;
using stocks_toolkit::PriceBar;
using stocks_toolkit::PriceStore;
using stocks_toolkit::Regime;
using stocks_toolkit::RegimeConfig;
using stocks_toolkit::RegimeDetector;

namespace {

PriceBar make_bar(std::string date, double close) {
    PriceBar bar;
    bar.date = std::move(date);
    bar.open = bar.high = bar.low = bar.close = close;
    bar.volume = 1000;
    return bar;
}

std::vector<PriceBar> make_closes(std::vector<double> closes) {
    std::vector<PriceBar> bars;
    int day = 1;
    for (double close : closes) {
        char date[11];
        std::snprintf(date, sizeof(date), "2024-01-%02d", day++);
        bars.push_back(make_bar(date, close));
    }
    return bars;
}

}  // namespace

TEST(ClassifyRegime, InsufficientDataWhenFewerBarsThanWindow) {
    RegimeConfig config;
    config.window_bars = 20;
    auto bars = make_closes({100, 101, 102});

    EXPECT_EQ(classify_regime(bars, config).regime, Regime::kInsufficientData);
}

TEST(ClassifyRegime, ExactlyWindowBarsIsSufficient) {
    RegimeConfig config;
    config.window_bars = 3;
    auto bars = make_closes({100, 101, 102});

    EXPECT_NE(classify_regime(bars, config).regime, Regime::kInsufficientData);
}

TEST(ClassifyRegime, ThrowsOnWindowSmallerThanTwo) {
    RegimeConfig config;
    config.window_bars = 1;
    auto bars = make_closes({100, 101});

    EXPECT_THROW(classify_regime(bars, config), std::invalid_argument);
}

TEST(ClassifyRegime, MonotonicMoveIsTrending) {
    RegimeConfig config;
    config.window_bars = 5;
    auto bars = make_closes({100, 101, 102, 103, 104});

    auto result = classify_regime(bars, config);
    EXPECT_EQ(result.regime, Regime::kTrending);
    EXPECT_DOUBLE_EQ(result.efficiency_ratio, 1.0);
}

TEST(ClassifyRegime, SmallOscillationIsRange) {
    RegimeConfig config;
    config.window_bars = 6;
    auto bars = make_closes({100, 101, 100, 101, 100, 101});

    auto result = classify_regime(bars, config);
    EXPECT_EQ(result.regime, Regime::kRange);
    EXPECT_NEAR(result.efficiency_ratio, 0.2, 1e-9);
    EXPECT_LT(result.containment_ratio, config.containment_range_threshold);
}

TEST(ClassifyRegime, DisagreeingSignalsAreUncertain) {
    RegimeConfig config;
    config.window_bars = 6;
    // ER ~0.43 (neither choppy nor trending) while containment stays low
    // (~0.99, "contained") — signals disagree, so this must not be
    // silently forced into Range or Trending.
    auto bars = make_closes({100, 102, 101, 103, 102, 103});

    auto result = classify_regime(bars, config);
    EXPECT_EQ(result.regime, Regime::kUncertain);
    EXPECT_NEAR(result.efficiency_ratio, 3.0 / 7.0, 1e-9);
}

TEST(ClassifyRegime, ChoppyErButBreakoutContainmentIsUncertain) {
    RegimeConfig config;
    config.window_bars = 37;
    // A symmetric V: down by 1 for 18 bars, then back up by 1 for 18 bars.
    // Net change is 0 (ER == 0, "choppy"), but the one-directional 18-point
    // dip travels much further than the day-to-day volatility would predict
    // (containment_ratio == 3.0, "breakout"). The two signals disagree, so
    // this must be Uncertain, not silently resolved to Trending.
    std::vector<double> closes;
    for (int i = 0; i <= 18; ++i) closes.push_back(100.0 - i);
    for (int i = 1; i <= 18; ++i) closes.push_back(82.0 + i);
    auto bars = make_closes(closes);

    auto result = classify_regime(bars, config);
    EXPECT_NEAR(result.efficiency_ratio, 0.0, 1e-9);
    EXPECT_NEAR(result.containment_ratio, 3.0, 1e-9);
    EXPECT_EQ(result.regime, Regime::kUncertain);
}

TEST(ClassifyRegime, TrendingErButContainedIsUncertain) {
    RegimeConfig config;
    config.window_bars = 6;
    // A steady ramp (ER == 1.0, "trending") with one outsized step. The
    // outsized step inflates the volatility estimate enough that the actual
    // range still looks "contained" relative to it. Signals disagree, so
    // this must be Uncertain, not silently resolved to Trending.
    auto bars = make_closes({100, 101, 102, 112, 113, 114});

    auto result = classify_regime(bars, config);
    EXPECT_DOUBLE_EQ(result.efficiency_ratio, 1.0);
    EXPECT_LT(result.containment_ratio, config.containment_range_threshold);
    EXPECT_EQ(result.regime, Regime::kUncertain);
}

TEST(ClassifyRegime, ZeroVarianceTrendIsBreakoutEvenAtSmallWindow) {
    RegimeConfig config;
    config.window_bars = 4;
    // Perfectly uniform steps => zero variance => expected_range is 0, so any
    // nonzero actual range must count as containment breakout, not a NaN or
    // silently-contained result.
    auto bars = make_closes({100, 101, 102, 103});

    auto result = classify_regime(bars, config);
    EXPECT_EQ(result.regime, Regime::kTrending);
    EXPECT_TRUE(std::isinf(result.containment_ratio));
}

TEST(RegimeDetectorClass, DelegatesToStoreWindowAndClassifies) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", make_closes({100, 101, 102, 103, 104}));

    RegimeConfig config;
    config.window_bars = 5;
    RegimeDetector detector(store, config);

    auto result = detector.detect("nvda.us", "2024-01-05");
    EXPECT_EQ(result.regime, Regime::kTrending);
}

TEST(RegimeDetectorClass, InsufficientDataWhenTickerHasNoHistory) {
    PriceStore store(":memory:");
    RegimeDetector detector(store, RegimeConfig{});

    EXPECT_EQ(detector.detect("unknown.us", "2024-01-05").regime, Regime::kInsufficientData);
}

TEST(RegimeDetectorClass, RespectsAsOfDateCutoff) {
    PriceStore store(":memory:");
    store.upsert_bars("nvda.us", make_closes({100, 101, 102, 103, 104}));

    RegimeConfig config;
    config.window_bars = 3;
    RegimeDetector detector(store, config);

    // As of day 3, only the first three (flat-ish) bars exist — the later
    // trending bars must not leak into the window.
    auto result = detector.detect("nvda.us", "2024-01-03");
    EXPECT_NE(result.regime, Regime::kInsufficientData);
    EXPECT_NEAR(result.efficiency_ratio, 1.0, 1e-9);  // 100,101,102 is still monotonic
}
