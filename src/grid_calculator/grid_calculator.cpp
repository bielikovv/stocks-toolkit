#include "stocks_toolkit/grid_calculator/grid_calculator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace stocks_toolkit {

namespace {

// Generous cap on atr_window_bars (~270 years of daily bars) that keeps
// `atr_window_bars + 1` and downstream arithmetic far from int overflow —
// without it, an extreme config value (e.g. INT_MAX) overflows that
// addition and can walk an iterator past the end of the bars vector.
constexpr int kMaxAtrWindowBars = 100000;

// A lower bound this close to zero relative to current_price would place
// geometric grid levels at economically meaningless prices with absurd
// implied quantities; treated as an invalid range, same as a non-positive
// lower bound.
constexpr double kMinLowerBoundFraction = 0.01;

void validate_config(const GridConfig& config) {
    if (config.atr_window_bars < 1 || config.atr_window_bars > kMaxAtrWindowBars) {
        throw std::invalid_argument("GridConfig.atr_window_bars must be between 1 and " +
                                     std::to_string(kMaxAtrWindowBars));
    }
    if (config.num_levels < 2) {
        throw std::invalid_argument("GridConfig.num_levels must be at least 2");
    }
    if (config.total_capital < 0.0) {
        throw std::invalid_argument("GridConfig.total_capital must not be negative");
    }
    if (config.range_atr_multiplier <= 0.0) {
        throw std::invalid_argument("GridConfig.range_atr_multiplier must be positive");
    }
}

// Average True Range: the mean, over `window.size() - 1` consecutive bar
// pairs, of each day's True Range (the largest of: today's high-low spread,
// or today's high/low vs. yesterday's close — captures gaps between days,
// not just the current day's candle).
double average_true_range(const std::vector<PriceBar>& window) {
    double sum_tr = 0.0;
    for (std::size_t i = 1; i < window.size(); ++i) {
        const PriceBar& bar = window[i];
        const PriceBar& prev = window[i - 1];
        double high_low = bar.high - bar.low;
        double high_prev_close = std::abs(bar.high - prev.close);
        double low_prev_close = std::abs(bar.low - prev.close);
        sum_tr += std::max({high_low, high_prev_close, low_prev_close});
    }
    return sum_tr / static_cast<double>(window.size() - 1);
}

}  // namespace

GridParams calculate_grid_params(const std::vector<PriceBar>& bars, const GridConfig& config) {
    validate_config(config);

    GridParams result;
    int required_bars = config.atr_window_bars + 1;
    if (static_cast<int>(bars.size()) < required_bars) {
        return result;  // status stays kInsufficientData
    }

    auto window_begin = bars.end() - required_bars;
    std::vector<PriceBar> window(window_begin, bars.end());

    result.current_price = window.back().close;
    result.atr = average_true_range(window);

    // `> 0.0`, not `<= 0.0`: comparisons against NaN are always false, so
    // this form is what actually rejects a NaN ATR (from e.g. corrupt input
    // data) in addition to a zero or negative one.
    if (!(result.atr > 0.0)) {
        result.status = GridCalculationStatus::kInvalidVolatility;
        return result;
    }

    result.lower_bound = result.current_price - config.range_atr_multiplier * result.atr;
    result.upper_bound = result.current_price + config.range_atr_multiplier * result.atr;

    // Same NaN-safe form as above, extended to also reject a lower bound
    // that's positive but too small relative to price to be meaningful.
    if (!(result.lower_bound > kMinLowerBoundFraction * result.current_price)) {
        result.status = GridCalculationStatus::kInvalidRange;
        return result;
    }

    double ratio =
        std::pow(result.upper_bound / result.lower_bound, 1.0 / (config.num_levels - 1));
    result.spacing_pct = ratio - 1.0;

    double capital_per_level = config.total_capital / config.num_levels;
    result.levels.reserve(config.num_levels);
    for (int i = 0; i < config.num_levels; ++i) {
        GridLevel level;
        level.price = result.lower_bound * std::pow(ratio, i);
        level.capital_allocated = capital_per_level;
        level.quantity = capital_per_level / level.price;
        result.levels.push_back(level);
    }

    result.status = GridCalculationStatus::kOk;
    return result;
}

GridParameterCalculator::GridParameterCalculator(PriceStore& store, GridConfig config)
    : store_(store), config_(config) {
    validate_config(config_);
}

GridParams GridParameterCalculator::calculate(const std::string& ticker,
                                               const std::string& as_of_date) const {
    std::vector<PriceBar> bars =
        store_.get_last_n_bars(ticker, as_of_date, config_.atr_window_bars + 1);
    return calculate_grid_params(bars, config_);
}

}  // namespace stocks_toolkit
