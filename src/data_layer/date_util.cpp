#include "stocks_toolkit/data_layer/date_util.h"

#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace stocks_toolkit {

int64_t iso_date_to_unix_seconds(const std::string& iso_date) {
    struct tm tm {};
    int year = 0, month = 0, day = 0;
    if (std::sscanf(iso_date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
        throw std::invalid_argument("invalid ISO date: " + iso_date);
    }
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    return static_cast<int64_t>(timegm(&tm));
}

std::string unix_seconds_to_iso_date(int64_t unix_seconds) {
    time_t t = static_cast<time_t>(unix_seconds);
    struct tm tm {};
    gmtime_r(&t, &tm);
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

int64_t now_unix_seconds() { return static_cast<int64_t>(std::time(nullptr)); }

}  // namespace stocks_toolkit
