#pragma once

#include <string_view>
#include <vector>

#include "stocks_toolkit/data_layer/price_bar.h"

namespace stocks_toolkit {

// Parses a Yahoo Finance v8 chart API JSON response into daily price bars.
// Bars with a null OHLCV field (holidays/gaps in Yahoo's own data) and any
// response that doesn't match the expected shape are skipped rather than
// aborting the whole parse — one bad ticker response shouldn't lose data
// for every other ticker in the run.
std::vector<PriceBar> parse_yahoo_chart_json(std::string_view json_text);

}  // namespace stocks_toolkit
