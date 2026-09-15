#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "stocks_toolkit/data_layer/curl_http_client.h"
#include "stocks_toolkit/data_layer/price_store.h"
#include "stocks_toolkit/data_layer/yahoo_fetcher.h"

namespace {

std::vector<std::string> read_tickers(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open ticker list: " + path);
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

}  // namespace

int main(int argc, char** argv) {
    std::string tickers_path = argc > 1 ? argv[1] : "config/tickers.txt";
    std::string db_path = argc > 2 ? argv[2] : "data/prices.db";

    std::vector<std::string> tickers;
    try {
        tickers = read_tickers(tickers_path);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }

    stocks_toolkit::CurlHttpClient http_client;
    stocks_toolkit::YahooFetcher fetcher(http_client);
    stocks_toolkit::PriceStore store(db_path);

    int ok_count = 0;
    int fail_count = 0;

    for (const auto& ticker : tickers) {
        try {
            auto last = store.last_date(ticker);
            std::vector<stocks_toolkit::PriceBar> bars =
                last.has_value() ? fetcher.fetch_since(ticker, *last)
                                  : fetcher.fetch_full_history(ticker);

            if (bars.empty()) {
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
                store.upsert_bars(ticker, bars);
                std::cout << ticker << ": stored " << bars.size() << " bars (" << bars.front().date
                          << " to " << bars.back().date << ")\n";
                ++ok_count;
            }
        } catch (const std::exception& e) {
            std::cerr << ticker << ": FAILED - " << e.what() << "\n";
            ++fail_count;
        }
    }

    std::cout << "\nDone: " << ok_count << " succeeded, " << fail_count << " failed, out of "
              << tickers.size() << " tickers.\n";
    return fail_count > 0 && ok_count == 0 ? 1 : 0;
}
