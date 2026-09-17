#pragma once

#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/price_bar.h"

namespace stocks_toolkit {

// Filters out bars that fail basic sanity checks: non-positive prices,
// high < low, an open/close outside [low, high], negative volume, or a date
// not strictly after the previous kept bar. Order is preserved. Each dropped
// bar is logged to stderr with its reason so a data quality issue doesn't
// get silently absorbed into storage.
std::vector<PriceBar> validate_bars(const std::string& ticker, std::vector<PriceBar> bars);

}  // namespace stocks_toolkit
