#pragma once

#include <vector>

#include "stocks_toolkit/data_layer/corporate_action.h"
#include "stocks_toolkit/data_layer/price_bar.h"

namespace stocks_toolkit {

// Everything the Yahoo chart endpoint returns for one ticker over one
// window: the price history and any dividends/splits that occurred in it.
// Kept as one struct because both come from a single parse of one response.
struct YahooChartData {
    std::vector<PriceBar> bars;
    std::vector<CorporateAction> corporate_actions;
};

}  // namespace stocks_toolkit
