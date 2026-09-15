#pragma once

#include <cstdint>
#include <string>

namespace stocks_toolkit {

// One daily OHLCV bar. `date` is always an ISO-8601 "YYYY-MM-DD" string —
// callers rely on lexicographic string comparison for date ordering, so
// this format must stay fixed-width and zero-padded.
struct PriceBar {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    int64_t volume = 0;

    bool operator==(const PriceBar&) const = default;
};

}  // namespace stocks_toolkit
