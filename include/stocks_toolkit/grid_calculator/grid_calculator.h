#pragma once

#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/price_bar.h"
#include "stocks_toolkit/data_layer/price_store.h"

namespace stocks_toolkit {

enum class GridCalculationStatus {
    kOk,
    kInsufficientData,

    // ATR came out zero (a perfectly flat window — e.g. a halted ticker, or
    // a provider forward-filling a holiday with the previous close) or
    // could not be computed as a real number. There is no meaningful
    // "typical daily move" to size a range from, so this is surfaced
    // explicitly rather than silently producing a zero-width range where
    // every level sits on the same price.
    kInvalidVolatility,

    // The computed lower bound (current_price - range_atr_multiplier * ATR)
    // is at, below, or too close to zero relative to current_price to be a
    // usable price. Surfaced explicitly rather than clamped to an arbitrary
    // floor, since a grid level at or near a zero price is meaningless —
    // deliberately distinct from a made-up "safe" answer.
    kInvalidRange,
};

// Tunable parameters for calculate_grid_params(). Defaults are reasonable
// starting points, not backtested-optimal values — M3 (backtester) is
// expected to validate/tune these against real outcomes, not this module.
struct GridConfig {
    // Trailing bars used to compute Average True Range (ATR). 14 is the
    // traditional ATR lookback used across most trading literature/tools.
    // Must be between 1 and 100,000 (a generous cap that keeps the internal
    // `+ 1` bar-count arithmetic far from integer overflow).
    int atr_window_bars = 14;

    // Grid range extends this many multiples of ATR above and below the
    // current price.
    double range_atr_multiplier = 2.0;

    // Fixed number of grid levels (buy/sell lines) placed across the range;
    // spacing is derived to fit exactly this many levels evenly in
    // percentage terms. Must be >= 2.
    int num_levels = 10;

    // Total capital split evenly across all levels. Must be >= 0.
    double total_capital = 0.0;
};

// One grid line: a price to trade at, the capital assigned to it, and the
// implied quantity (capital_allocated / price) that capital buys there.
struct GridLevel {
    double price = 0.0;
    double capital_allocated = 0.0;
    double quantity = 0.0;
};

struct GridParams {
    GridCalculationStatus status = GridCalculationStatus::kInsufficientData;

    // Most recent close in the window — the reference point the range is
    // centered on. Not necessarily itself a grid level.
    double current_price = 0.0;

    // Average True Range over the trailing `config.atr_window_bars` bars.
    double atr = 0.0;

    double lower_bound = 0.0;
    double upper_bound = 0.0;

    // Percentage gap between adjacent grid levels (e.g. 0.01 = 1%).
    double spacing_pct = 0.0;

    // Ordered lower to upper. Populated (size == config.num_levels) only
    // when status == kOk; empty otherwise.
    std::vector<GridLevel> levels;
};

// Computes grid parameters from a trailing window of daily bars (`bars`
// must be ordered oldest-to-newest). ATR needs `config.atr_window_bars + 1`
// bars (one extra for the "previous close" of the first True Range value);
// bars beyond that trailing window are ignored. Returns kInsufficientData
// if fewer bars are supplied. Pure function — no I/O — so it's directly
// testable with synthetic price series.
GridParams calculate_grid_params(const std::vector<PriceBar>& bars,
                                  const GridConfig& config = GridConfig{});

// Thin orchestration layer: pulls the trailing window for `ticker` from a
// PriceStore and delegates to calculate_grid_params().
class GridParameterCalculator {
public:
    // Throws std::invalid_argument immediately if `config` is invalid,
    // rather than deferring to the first calculate() call.
    explicit GridParameterCalculator(PriceStore& store, GridConfig config = GridConfig{});

    // Grid params as of `as_of_date` (inclusive), using the bars stored for
    // `ticker` at or before that date.
    GridParams calculate(const std::string& ticker, const std::string& as_of_date) const;

private:
    PriceStore& store_;
    GridConfig config_;
};

}  // namespace stocks_toolkit
