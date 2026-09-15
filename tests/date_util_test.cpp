#include "stocks_toolkit/data_layer/date_util.h"

#include <gtest/gtest.h>

using stocks_toolkit::iso_date_to_unix_seconds;
using stocks_toolkit::unix_seconds_to_iso_date;

TEST(DateUtil, EpochRoundTrips) {
    EXPECT_EQ(iso_date_to_unix_seconds("1970-01-01"), 0);
    EXPECT_EQ(unix_seconds_to_iso_date(0), "1970-01-01");
}

TEST(DateUtil, KnownDateConvertsToExpectedTimestamp) {
    EXPECT_EQ(iso_date_to_unix_seconds("2024-01-02"), 1704153600);
    EXPECT_EQ(unix_seconds_to_iso_date(1704153600), "2024-01-02");
}

TEST(DateUtil, RoundTripIsStable) {
    for (const std::string& date : {"1999-12-31", "2020-02-29", "2026-09-15"}) {
        EXPECT_EQ(unix_seconds_to_iso_date(iso_date_to_unix_seconds(date)), date);
    }
}

TEST(DateUtil, RejectsMalformedDate) {
    EXPECT_THROW(iso_date_to_unix_seconds("not-a-date"), std::invalid_argument);
}
