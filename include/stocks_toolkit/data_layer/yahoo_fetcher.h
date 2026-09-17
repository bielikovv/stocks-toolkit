#pragma once

#include <string>

#include "stocks_toolkit/data_layer/http_client.h"
#include "stocks_toolkit/data_layer/yahoo_chart_data.h"

namespace stocks_toolkit {

// Fetches daily historical bars and corporate actions for a ticker from the
// Yahoo Finance chart API. Takes the HTTP client as a dependency so tests
// can inject a fake one instead of hitting the network.
//
// Lifetime contract: `http_client` is stored by reference and is not owned.
// The caller must ensure it outlives this YahooFetcher — e.g. don't return a
// YahooFetcher built from a locally-scoped IHttpClient, and don't store one
// longer than the client it was built with.
class YahooFetcher {
public:
    explicit YahooFetcher(IHttpClient& http_client) : http_client_(http_client) {}

    // Fetches the full available history for `ticker` (e.g. "NVDA").
    YahooChartData fetch_full_history(const std::string& ticker);

    // Fetches history for `ticker` from `since_date` (inclusive, "YYYY-MM-DD")
    // through now. Re-fetching `since_date` itself is intentional and
    // harmless — the store's upsert is idempotent on (ticker, date).
    YahooChartData fetch_since(const std::string& ticker, const std::string& since_date);

private:
    IHttpClient& http_client_;
};

}  // namespace stocks_toolkit
