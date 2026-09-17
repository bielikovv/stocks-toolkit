#pragma once

#include <string_view>

#include "stocks_toolkit/data_layer/yahoo_chart_data.h"

namespace stocks_toolkit {

// Parses a Yahoo Finance v8 chart API JSON response into daily price bars
// plus any dividends/splits in the requested window. Bars with a null
// OHLCV field (holidays/gaps in Yahoo's own data) and any response that
// doesn't match the expected shape are skipped rather than aborting the
// whole parse — one bad ticker response shouldn't lose data for every other
// ticker in the run. A malformed `events` block is likewise skipped on its
// own without discarding otherwise-valid price bars.
YahooChartData parse_yahoo_chart_json(std::string_view json_text);

}  // namespace stocks_toolkit
