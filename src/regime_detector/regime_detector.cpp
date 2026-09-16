#include "stocks_toolkit/regime_detector/regime_detector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace stocks_toolkit {

RegimeResult classify_regime(const std::vector<PriceBar>& bars, const RegimeConfig& config) {
    if (config.window_bars < 2) {
        throw std::invalid_argument("RegimeConfig.window_bars must be at least 2");
    }

    RegimeResult result;
    if (static_cast<int>(bars.size()) < config.window_bars) {
        return result;  // regime stays kInsufficientData
    }

    auto window_begin = bars.end() - config.window_bars;

    std::vector<double> changes;
    changes.reserve(config.window_bars - 1);
    for (auto it = window_begin + 1; it != bars.end(); ++it) {
        changes.push_back(it->close - (it - 1)->close);
    }

    double sum_abs_changes =
        std::accumulate(changes.begin(), changes.end(), 0.0,
                         [](double acc, double c) { return acc + std::abs(c); });
    double net_change = std::abs(bars.back().close - window_begin->close);
    result.efficiency_ratio = sum_abs_changes > 0.0 ? net_change / sum_abs_changes : 0.0;

    double mean_change = std::accumulate(changes.begin(), changes.end(), 0.0) /
                          static_cast<double>(changes.size());
    double variance =
        std::accumulate(changes.begin(), changes.end(), 0.0,
                         [mean_change](double acc, double c) {
                             double d = c - mean_change;
                             return acc + d * d;
                         }) /
        static_cast<double>(changes.size());
    double stdev = std::sqrt(variance);
    double expected_range = stdev * std::sqrt(static_cast<double>(changes.size()));

    auto [min_it, max_it] = std::minmax_element(
        window_begin, bars.end(), [](const PriceBar& a, const PriceBar& b) { return a.close < b.close; });
    double actual_range = max_it->close - min_it->close;

    if (expected_range > 0.0) {
        result.containment_ratio = actual_range / expected_range;
    } else {
        result.containment_ratio =
            actual_range > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
    }

    bool choppy = result.efficiency_ratio <= config.efficiency_ratio_range_threshold;
    bool trending_by_er = result.efficiency_ratio >= config.efficiency_ratio_trend_threshold;
    bool contained = result.containment_ratio <= config.containment_range_threshold;
    bool breakout = result.containment_ratio >= config.containment_trend_threshold;

    if (choppy && contained) {
        result.regime = Regime::kRange;
    } else if (trending_by_er && breakout) {
        result.regime = Regime::kTrending;
    } else {
        result.regime = Regime::kUncertain;
    }

    return result;
}

RegimeDetector::RegimeDetector(PriceStore& store, RegimeConfig config)
    : store_(store), config_(config) {}

RegimeResult RegimeDetector::detect(const std::string& ticker, const std::string& as_of_date) const {
    std::vector<PriceBar> bars = store_.get_last_n_bars(ticker, as_of_date, config_.window_bars);
    return classify_regime(bars, config_);
}

}  // namespace stocks_toolkit
