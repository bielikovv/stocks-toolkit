#pragma once

#include <cstdint>
#include <string>

namespace stocks_toolkit {

// One daily OHLCV bar. `date` is always an ISO-8601 "YYYY-MM-DD" string —
// callers rely on lexicographic string comparison for date ordering, so
// this format must stay fixed-width and zero-padded.
//
// `close` is the raw traded close; `adj_close` is back-adjusted for
// dividends and splits and is what all return/performance math must use —
// raw `close` alone understates returns for any dividend-paying stock.
// When the provider doesn't supply an adjusted value for a bar, `adj_close`
// falls back to `close`.
struct PriceBar {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double adj_close = 0.0;
    int64_t volume = 0;

    bool operator==(const PriceBar&) const = default;
};

}  // namespace stocks_toolkit
