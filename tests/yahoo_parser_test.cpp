#include "stocks_toolkit/data_layer/yahoo_parser.h"

#include <gtest/gtest.h>

using stocks_toolkit::parse_yahoo_chart_json;

TEST(YahooParser, ParsesWellFormedResponse) {
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600, 1704240000],
                "indicators": {
                    "quote": [{
                        "open":   [10.0, 11.0],
                        "high":   [10.5, 11.5],
                        "low":    [9.5, 10.5],
                        "close":  [10.2, 11.2],
                        "volume": [1000, 2000]
                    }]
                }
            }],
            "error": null
        }
    })";

    auto bars = parse_yahoo_chart_json(json);

    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].date, "2024-01-02");
    EXPECT_DOUBLE_EQ(bars[0].close, 10.2);
    EXPECT_EQ(bars[0].volume, 1000);
    EXPECT_EQ(bars[1].date, "2024-01-03");
}

TEST(YahooParser, SkipsBarsWithNullFields) {
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600, 1704240000],
                "indicators": {
                    "quote": [{
                        "open":   [10.0, null],
                        "high":   [10.5, null],
                        "low":    [9.5, null],
                        "close":  [10.2, null],
                        "volume": [1000, null]
                    }]
                }
            }],
            "error": null
        }
    })";

    auto bars = parse_yahoo_chart_json(json);

    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(bars[0].date, "2024-01-02");
}

TEST(YahooParser, ReturnsEmptyOnApiError) {
    std::string json = R"({
        "chart": {
            "result": null,
            "error": {"code": "Not Found", "description": "No data found"}
        }
    })";

    EXPECT_TRUE(parse_yahoo_chart_json(json).empty());
}

TEST(YahooParser, ReturnsEmptyOnInvalidJson) {
    EXPECT_TRUE(parse_yahoo_chart_json("this is not json").empty());
}

TEST(YahooParser, ReturnsEmptyOnUnexpectedShape) {
    std::string json = R"({"chart": {"result": [{"unexpected": true}], "error": null}})";
    EXPECT_TRUE(parse_yahoo_chart_json(json).empty());
}

TEST(YahooParser, ReturnsEmptyWhenQuoteArrayIsEmpty) {
    // Regression test: previously indexed quote[0] unconditionally, which is
    // out-of-bounds UB on an empty array instead of a safe, reported failure.
    std::string json = R"({
        "chart": {
            "result": [{"timestamp": [1704153600], "indicators": {"quote": []}}],
            "error": null
        }
    })";

    EXPECT_TRUE(parse_yahoo_chart_json(json).empty());
}

TEST(YahooParser, ReturnsEmptyWhenOhlcvArrayIsShorterThanTimestamps) {
    // Regression test: previously indexed opens/highs/.../volumes up to
    // timestamps.size()-1 unconditionally, which is out-of-bounds UB if any
    // array is shorter (e.g. a truncated upstream response).
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600, 1704240000],
                "indicators": {
                    "quote": [{
                        "open":   [10.0],
                        "high":   [10.5, 11.5],
                        "low":    [9.5, 10.5],
                        "close":  [10.2, 11.2],
                        "volume": [1000, 2000]
                    }]
                }
            }],
            "error": null
        }
    })";

    EXPECT_TRUE(parse_yahoo_chart_json(json).empty());
}
