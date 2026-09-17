#include "stocks_toolkit/data_layer/yahoo_parser.h"

#include <nlohmann/json.hpp>

#include <iostream>

#include "stocks_toolkit/data_layer/date_util.h"

namespace stocks_toolkit {

namespace {

// Parses events.dividends / events.splits into CorporateAction records.
// Isolated in its own try/catch so a malformed events block can't discard
// otherwise-valid price bars parsed earlier in the same response.
std::vector<CorporateAction> parse_events(const nlohmann::json& result) {
    std::vector<CorporateAction> actions;
    if (!result.contains("events")) return actions;

    try {
        const auto& events = result.at("events");

        if (events.contains("dividends")) {
            for (const auto& [key, div] : events.at("dividends").items()) {
                CorporateAction action;
                action.type = CorporateActionType::kDividend;
                action.date = unix_seconds_to_iso_date(div.at("date").get<int64_t>());
                action.amount = div.at("amount").get<double>();
                actions.push_back(std::move(action));
            }
        }
        if (events.contains("splits")) {
            for (const auto& [key, split] : events.at("splits").items()) {
                CorporateAction action;
                action.type = CorporateActionType::kSplit;
                action.date = unix_seconds_to_iso_date(split.at("date").get<int64_t>());
                action.split_numerator = split.at("numerator").get<double>();
                action.split_denominator = split.at("denominator").get<double>();
                actions.push_back(std::move(action));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "yahoo_parser: malformed events block, skipping corporate actions: "
                   << e.what() << "\n";
        return {};
    }

    return actions;
}

}  // namespace

YahooChartData parse_yahoo_chart_json(std::string_view json_text) {
    YahooChartData data;

    auto root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        std::cerr << "yahoo_parser: response was not valid JSON\n";
        return data;
    }

    try {
        const auto& chart = root.at("chart");
        if (chart.contains("error") && !chart.at("error").is_null()) {
            std::cerr << "yahoo_parser: API returned an error: " << chart.at("error").dump()
                       << "\n";
            return data;
        }

        const auto& results = chart.at("result");
        if (!results.is_array() || results.empty()) {
            std::cerr << "yahoo_parser: no result in response\n";
            return data;
        }

        const auto& result = results.at(0);
        const auto& timestamps = result.at("timestamp");
        const auto& indicators = result.at("indicators");
        const auto& quotes = indicators.at("quote");
        if (!quotes.is_array() || quotes.empty()) {
            std::cerr << "yahoo_parser: indicators.quote is empty\n";
            return data;
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
            return data;
        }

        // Dividend/split-adjusted close, parallel to `timestamps` when present.
        // Optional and independently sized/nulled from the raw OHLCV arrays, so
        // its absence — or any unexpected shape here — must never cost us the
        // (more fundamental) raw price data already parsed above. Guarded with
        // .contains()/is_object() checks rather than .at(), and a local catch as
        // a last resort, so a malformed adjclose block can only skip adjclose.
        const nlohmann::json* adj_closes = nullptr;
        try {
            if (indicators.contains("adjclose") && indicators.at("adjclose").is_array() &&
                !indicators.at("adjclose").empty() &&
                indicators.at("adjclose").at(0).is_object() &&
                indicators.at("adjclose").at(0).contains("adjclose")) {
                const auto& candidate = indicators.at("adjclose").at(0).at("adjclose");
                if (candidate.is_array() && candidate.size() == n) {
                    adj_closes = &candidate;
                } else {
                    std::cerr << "yahoo_parser: adjclose array missing/mismatched size, falling "
                                 "back to raw close\n";
                }
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "yahoo_parser: malformed adjclose block, falling back to raw close: "
                       << e.what() << "\n";
            adj_closes = nullptr;
        }

        data.bars.reserve(n);
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
            bar.adj_close = (adj_closes && !adj_closes->at(i).is_null())
                                 ? adj_closes->at(i).get<double>()
                                 : bar.close;
            data.bars.push_back(std::move(bar));
        }

        data.corporate_actions = parse_events(result);
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "yahoo_parser: unexpected response shape: " << e.what() << "\n";
        return {};
    }

    return data;
}

}  // namespace stocks_toolkit
