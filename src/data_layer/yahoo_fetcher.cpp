#include "stocks_toolkit/data_layer/yahoo_fetcher.h"

#include "stocks_toolkit/data_layer/date_util.h"
#include "stocks_toolkit/data_layer/yahoo_parser.h"

namespace stocks_toolkit {

namespace {

// Always pass explicit period1/period2 with interval=1d — asking Yahoo for
// interval=1d combined with a symbolic range like "max" silently downgrades
// the response to monthly bars instead of erroring, which is easy to miss.
std::string build_url(const std::string& ticker, int64_t period1, int64_t period2) {
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + ticker +
           "?interval=1d&period1=" + std::to_string(period1) +
           "&period2=" + std::to_string(period2);
}

}  // namespace

std::vector<PriceBar> YahooFetcher::fetch_full_history(const std::string& ticker) {
    std::string url = build_url(ticker, 0, now_unix_seconds());
    std::string body = http_client_.get(url);
    return parse_yahoo_chart_json(body);
}

std::vector<PriceBar> YahooFetcher::fetch_since(const std::string& ticker,
                                                 const std::string& since_date) {
    std::string url = build_url(ticker, iso_date_to_unix_seconds(since_date), now_unix_seconds());
    std::string body = http_client_.get(url);
    return parse_yahoo_chart_json(body);
}

}  // namespace stocks_toolkit
