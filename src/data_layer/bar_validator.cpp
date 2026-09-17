#include "stocks_toolkit/data_layer/bar_validator.h"

#include <iostream>

namespace stocks_toolkit {

namespace {

bool is_positive(double value) { return value > 0.0; }

// nullptr if `bar` is sane, otherwise a short reason string.
const char* reject_reason(const PriceBar& bar, const std::string& previous_date) {
    if (!is_positive(bar.open) || !is_positive(bar.high) || !is_positive(bar.low) ||
        !is_positive(bar.close) || !is_positive(bar.adj_close)) {
        return "non-positive price";
    }
    if (bar.high < bar.low) {
        return "high < low";
    }
    if (bar.open > bar.high || bar.open < bar.low || bar.close > bar.high ||
        bar.close < bar.low) {
        return "open/close outside [low, high]";
    }
    if (bar.volume < 0) {
        return "negative volume";
    }
    if (!previous_date.empty() && bar.date <= previous_date) {
        return "date not strictly increasing";
    }
    return nullptr;
}

}  // namespace

std::vector<PriceBar> validate_bars(const std::string& ticker, std::vector<PriceBar> bars) {
    std::vector<PriceBar> valid;
    valid.reserve(bars.size());
    std::string previous_date;

    for (auto& bar : bars) {
        const char* reason = reject_reason(bar, previous_date);
        if (reason) {
            std::cerr << "bar_validator: " << ticker << " " << bar.date << ": dropped (" << reason
                       << ")\n";
            continue;
        }
        previous_date = bar.date;
        valid.push_back(std::move(bar));
    }
    return valid;
}

}  // namespace stocks_toolkit
