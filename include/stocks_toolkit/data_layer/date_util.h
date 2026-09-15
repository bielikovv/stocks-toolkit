#pragma once

#include <cstdint>
#include <string>

namespace stocks_toolkit {

// Converts an ISO-8601 "YYYY-MM-DD" date (interpreted as UTC midnight) to a
// Unix timestamp in seconds.
int64_t iso_date_to_unix_seconds(const std::string& iso_date);

// Converts a Unix timestamp in seconds to an ISO-8601 "YYYY-MM-DD" date,
// using the UTC calendar date at that instant.
std::string unix_seconds_to_iso_date(int64_t unix_seconds);

// Current time as a Unix timestamp in seconds.
int64_t now_unix_seconds();

}  // namespace stocks_toolkit
