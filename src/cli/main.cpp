#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "stocks_toolkit/data_layer/bar_validator.h"
#include "stocks_toolkit/data_layer/curl_http_client.h"
#include "stocks_toolkit/data_layer/http_client.h"
#include "stocks_toolkit/data_layer/price_store.h"
#include "stocks_toolkit/data_layer/yahoo_fetcher.h"

namespace {

using stocks_toolkit::CurlHttpClient;
using stocks_toolkit::HttpError;
using stocks_toolkit::PriceStore;
using stocks_toolkit::validate_bars;
using stocks_toolkit::YahooChartData;
using stocks_toolkit::YahooFetcher;

constexpr int kMaxFetchAttempts = 3;
constexpr auto kRetryBaseBackoff = std::chrono::milliseconds(500);
constexpr auto kDelayBetweenTickers = std::chrono::milliseconds(250);

// Reads one ticker per line, skipping blank lines and lines starting with
// '#'. A missing `required` file is fatal; a missing optional one (the
// benchmarks list) just means "no benchmarks configured" for this run.
std::vector<std::string> read_ticker_list(const std::string& path, bool required) {
    std::ifstream file(path);
    if (!file) {
        if (required) {
            throw std::runtime_error("could not open ticker list: " + path);
        }
        std::cerr << "note: '" << path << "' not found, continuing without benchmark tickers\n";
        return {};
    }
    std::vector<std::string> tickers;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            tickers.push_back(line);
        }
    }
    return tickers;
}

// Ticker fetch order, deduplicated. Benchmarks are listed first and win on
// overlap with the main ticker list, since being "the market" is the more
// specific fact about that symbol.
std::vector<std::pair<std::string, bool>> build_universe(
    const std::vector<std::string>& benchmarks, const std::vector<std::string>& tickers) {
    std::vector<std::pair<std::string, bool>> universe;
    std::unordered_set<std::string> seen;
    for (const auto& ticker : benchmarks) {
        if (seen.insert(ticker).second) universe.emplace_back(ticker, true);
    }
    for (const auto& ticker : tickers) {
        if (seen.insert(ticker).second) universe.emplace_back(ticker, false);
    }
    return universe;
}

// Retries on transport failure (timeouts, DNS hiccups, throttling) with
// linear backoff — a batch of hundreds of tickers will hit transient network
// errors that a single unlucky ticker shouldn't turn into a permanent
// failure. Does not retry on a malformed/error response body; the parser
// already handles that by returning an empty result, not by throwing.
YahooChartData fetch_with_retry(YahooFetcher& fetcher, const std::string& ticker,
                                 const std::optional<std::string>& since_date) {
    for (int attempt = 1;; ++attempt) {
        try {
            return since_date.has_value() ? fetcher.fetch_since(ticker, *since_date)
                                           : fetcher.fetch_full_history(ticker);
        } catch (const HttpError& e) {
            if (attempt >= kMaxFetchAttempts) throw;
            std::cerr << ticker << ": attempt " << attempt << " failed (" << e.what()
                       << "), retrying...\n";
            std::this_thread::sleep_for(kRetryBaseBackoff * attempt);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string tickers_path = argc > 1 ? argv[1] : "config/tickers.txt";
    std::string benchmarks_path = argc > 2 ? argv[2] : "config/benchmarks.txt";
    std::string db_path = argc > 3 ? argv[3] : "data/prices.db";

    std::vector<std::pair<std::string, bool>> universe;
    try {
        auto tickers = read_ticker_list(tickers_path, /*required=*/true);
        auto benchmarks = read_ticker_list(benchmarks_path, /*required=*/false);
        universe = build_universe(benchmarks, tickers);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }

    CurlHttpClient http_client;
    YahooFetcher fetcher(http_client);
    PriceStore store(db_path);

    int ok_count = 0;
    int fail_count = 0;

    for (size_t i = 0; i < universe.size(); ++i) {
        const auto& [ticker, is_benchmark] = universe[i];
        try {
            auto last = store.last_date(ticker);
            YahooChartData data = fetch_with_retry(fetcher, ticker, last);

            if (data.bars.empty()) {
                if (last.has_value()) {
                    // Normal on a re-run the same day — nothing new since last_date yet.
                    std::cout << ticker << ": no new data\n";
                    ++ok_count;
                } else {
                    // A first-ever fetch returning zero bars is not normal — it almost
                    // always means the response wasn't usable data (parser already
                    // logged why to stderr), not that the ticker truly has no history.
                    std::cerr << ticker << ": FAILED - full history fetch returned no bars\n";
                    ++fail_count;
                }
            } else {
                auto valid_bars = validate_bars(ticker, std::move(data.bars));
                if (valid_bars.empty()) {
                    std::cerr << ticker << ": FAILED - all fetched bars failed validation\n";
                    ++fail_count;
                } else {
                    store.upsert_bars(ticker, valid_bars);
                    store.upsert_corporate_actions(ticker, data.corporate_actions);
                    std::cout << ticker << ": stored " << valid_bars.size() << " bars ("
                               << valid_bars.front().date << " to " << valid_bars.back().date
                               << "), " << data.corporate_actions.size()
                               << " corporate action(s)\n";
                    ++ok_count;
                }
            }
            store.set_is_benchmark(ticker, is_benchmark);
        } catch (const std::exception& e) {
            std::cerr << ticker << ": FAILED - " << e.what() << "\n";
            ++fail_count;
        }

        if (i + 1 < universe.size()) {
            std::this_thread::sleep_for(kDelayBetweenTickers);
        }
    }

    std::cout << "\nDone: " << ok_count << " succeeded, " << fail_count << " failed, out of "
               << universe.size() << " tickers.\n";
    return fail_count > 0 && ok_count == 0 ? 1 : 0;
}
