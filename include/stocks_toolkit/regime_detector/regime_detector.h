#pragma once

#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/price_bar.h"
#include "stocks_toolkit/data_layer/price_store.h"

namespace stocks_toolkit {

// Classification of an asset's recent price behavior. kUncertain means the
// two underlying signals (trend strength and range containment) disagree —
// deliberately distinct from kRange/kTrending rather than forced into one,
// since a false "confident" read is worse than an honest "don't know".
enum class Regime {
    kRange,
    kTrending,
    kUncertain,
    kInsufficientData,
};

// Tunable parameters for classify_regime(). Defaults are reasonable starting
// points, not backtested-optimal values — M3 (backtester) is expected to
// validate/tune these against real outcomes, not this module.
struct RegimeConfig {
    // Number of trailing daily closes the analysis is computed over.
    int window_bars = 20;

    // Efficiency Ratio <= this => the "trend strength" signal says choppy.
    double efficiency_ratio_range_threshold = 0.3;
    // Efficiency Ratio >= this => the "trend strength" signal says trending.
    double efficiency_ratio_trend_threshold = 0.5;

    // Containment ratio <= this => the "range containment" signal says contained.
    double containment_range_threshold = 2.0;
    // Containment ratio >= this => the "range containment" signal says breakout.
    double containment_trend_threshold = 3.0;
};

struct RegimeResult {
    Regime regime = Regime::kInsufficientData;

    // Kaufman's Efficiency Ratio over the window: |net close change| / sum of
    // |bar-to-bar close changes|. 0 = pure chop, 1 = monotonic move.
    double efficiency_ratio = 0.0;

    // Actual close-price range over the window, divided by the range a pure
    // random walk with the window's observed daily volatility would be
    // expected to cover. Roughly 1 = moved about as far as volatility alone
    // predicts (contained); much greater than 1 = travelled further than
    // volatility explains (breakout/trend).
    double containment_ratio = 0.0;
};

// Classifies the regime of the trailing `config.window_bars` bars in `bars`
// (`bars` must be ordered oldest-to-newest; bars beyond the window are
// ignored). Returns kInsufficientData if fewer than `config.window_bars`
// bars are supplied. Pure function — no I/O — so it's directly testable with
// synthetic price series.
RegimeResult classify_regime(const std::vector<PriceBar>& bars,
                              const RegimeConfig& config = RegimeConfig{});

// Thin orchestration layer: pulls the trailing window for `ticker` from a
// PriceStore and delegates to classify_regime().
class RegimeDetector {
public:
    explicit RegimeDetector(PriceStore& store, RegimeConfig config = RegimeConfig{});

    // Regime as of `as_of_date` (inclusive), using the `config.window_bars`
    // most recent bars stored for `ticker` at or before that date.
    RegimeResult detect(const std::string& ticker, const std::string& as_of_date) const;

private:
    PriceStore& store_;
    RegimeConfig config_;
};

}  // namespace stocks_toolkit
