#include "stocks_toolkit/data_layer/yahoo_parser.h"

#include <nlohmann/json.hpp>

#include <iostream>

#include "stocks_toolkit/data_layer/date_util.h"

namespace stocks_toolkit {

std::vector<PriceBar> parse_yahoo_chart_json(std::string_view json_text) {
    std::vector<PriceBar> bars;

    auto root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        std::cerr << "yahoo_parser: response was not valid JSON\n";
        return bars;
    }

    try {
        const auto& chart = root.at("chart");
        if (chart.contains("error") && !chart.at("error").is_null()) {
            std::cerr << "yahoo_parser: API returned an error: " << chart.at("error").dump()
                       << "\n";
            return bars;
        }

        const auto& results = chart.at("result");
        if (!results.is_array() || results.empty()) {
            std::cerr << "yahoo_parser: no result in response\n";
            return bars;
        }

        const auto& result = results.at(0);
        const auto& timestamps = result.at("timestamp");
        const auto& quotes = result.at("indicators").at("quote");
        if (!quotes.is_array() || quotes.empty()) {
            std::cerr << "yahoo_parser: indicators.quote is empty\n";
            return bars;
        }
        const auto& quote = quotes.at(0);
        const auto& opens = quote.at("open");
        const auto& highs = quote.at("high");
        const auto& lows = quote.at("low");
        const auto& closes = quote.at("close");
        const auto& volumes = quote.at("volume");

        // Every OHLCV array is expected to be parallel to `timestamps`. A provider
        // response that's truncated or malformed in transit could disagree — catch
        // that explicitly rather than indexing past the shorter array.
        const size_t n = timestamps.size();
        if (opens.size() != n || highs.size() != n || lows.size() != n || closes.size() != n ||
            volumes.size() != n) {
            std::cerr << "yahoo_parser: OHLCV array length mismatch with timestamp array\n";
            return bars;
        }

        bars.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (opens.at(i).is_null() || highs.at(i).is_null() || lows.at(i).is_null() ||
                closes.at(i).is_null() || volumes.at(i).is_null()) {
                continue;  // market holiday / data gap for this bar
            }
            PriceBar bar;
            bar.date = unix_seconds_to_iso_date(timestamps.at(i).get<int64_t>());
            bar.open = opens.at(i).get<double>();
            bar.high = highs.at(i).get<double>();
            bar.low = lows.at(i).get<double>();
            bar.close = closes.at(i).get<double>();
            bar.volume = volumes.at(i).get<int64_t>();
            bars.push_back(std::move(bar));
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "yahoo_parser: unexpected response shape: " << e.what() << "\n";
        return {};
    }

    return bars;
}

}  // namespace stocks_toolkit
