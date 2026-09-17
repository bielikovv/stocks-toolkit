#include "stocks_toolkit/data_layer/yahoo_parser.h"

#include <gtest/gtest.h>

#include <algorithm>

using stocks_toolkit::CorporateActionType;
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

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 2u);
    EXPECT_EQ(data.bars[0].date, "2024-01-02");
    EXPECT_DOUBLE_EQ(data.bars[0].close, 10.2);
    EXPECT_EQ(data.bars[0].volume, 1000);
    EXPECT_EQ(data.bars[1].date, "2024-01-03");
    EXPECT_TRUE(data.corporate_actions.empty());
}

TEST(YahooParser, FallsBackToCloseWhenAdjcloseMissing) {
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600],
                "indicators": {
                    "quote": [{
                        "open": [10.0], "high": [10.5], "low": [9.5], "close": [10.2],
                        "volume": [1000]
                    }]
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(data.bars[0].adj_close, 10.2);
}

TEST(YahooParser, UsesAdjcloseWhenPresent) {
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
                    }],
                    "adjclose": [{
                        "adjclose": [9.8, null]
                    }]
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 2u);
    EXPECT_DOUBLE_EQ(data.bars[0].adj_close, 9.8);
    // Null adjclose for this bar falls back to raw close rather than dropping the bar.
    EXPECT_DOUBLE_EQ(data.bars[1].adj_close, 11.2);
}

TEST(YahooParser, IgnoresAdjcloseWhenSizeMismatched) {
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
                    }],
                    "adjclose": [{
                        "adjclose": [9.8]
                    }]
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 2u);
    EXPECT_DOUBLE_EQ(data.bars[0].adj_close, 10.2);
    EXPECT_DOUBLE_EQ(data.bars[1].adj_close, 11.2);
}

TEST(YahooParser, MalformedAdjcloseShapeDoesNotDiscardBars) {
    // Regression test: indicators.adjclose[0].adjclose was accessed with .at(),
    // which throws (and previously wiped out already-parsed bars via the outer
    // catch) if adjclose[0] isn't an object containing an "adjclose" key.
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600],
                "indicators": {
                    "quote": [{
                        "open": [10.0], "high": [10.5], "low": [9.5], "close": [10.2],
                        "volume": [1000]
                    }],
                    "adjclose": [{}]
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(data.bars[0].adj_close, 10.2);
}

TEST(YahooParser, NonObjectAdjcloseEntryDoesNotDiscardBars) {
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600],
                "indicators": {
                    "quote": [{
                        "open": [10.0], "high": [10.5], "low": [9.5], "close": [10.2],
                        "volume": [1000]
                    }],
                    "adjclose": [null]
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(data.bars[0].adj_close, 10.2);
}

TEST(YahooParser, ParsesDividendsAndSplits) {
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600],
                "indicators": {
                    "quote": [{
                        "open": [10.0], "high": [10.5], "low": [9.5], "close": [10.2],
                        "volume": [1000]
                    }]
                },
                "events": {
                    "dividends": {
                        "1704153600": {"amount": 0.24, "date": 1704153600}
                    },
                    "splits": {
                        "1704153600": {"date": 1704153600, "numerator": 4.0, "denominator": 1.0,
                                         "splitRatio": "4:1"}
                    }
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.corporate_actions.size(), 2u);
    auto dividend = std::find_if(data.corporate_actions.begin(), data.corporate_actions.end(),
                                   [](const auto& a) { return a.type == CorporateActionType::kDividend; });
    ASSERT_NE(dividend, data.corporate_actions.end());
    EXPECT_EQ(dividend->date, "2024-01-02");
    EXPECT_DOUBLE_EQ(dividend->amount, 0.24);

    auto split = std::find_if(data.corporate_actions.begin(), data.corporate_actions.end(),
                                [](const auto& a) { return a.type == CorporateActionType::kSplit; });
    ASSERT_NE(split, data.corporate_actions.end());
    EXPECT_DOUBLE_EQ(split->split_numerator, 4.0);
    EXPECT_DOUBLE_EQ(split->split_denominator, 1.0);
}

TEST(YahooParser, MalformedEventsDoesNotDiscardValidBars) {
    std::string json = R"({
        "chart": {
            "result": [{
                "timestamp": [1704153600],
                "indicators": {
                    "quote": [{
                        "open": [10.0], "high": [10.5], "low": [9.5], "close": [10.2],
                        "volume": [1000]
                    }]
                },
                "events": {
                    "dividends": {
                        "1704153600": {"amount": "not-a-number", "date": 1704153600}
                    }
                }
            }],
            "error": null
        }
    })";

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 1u);
    EXPECT_TRUE(data.corporate_actions.empty());
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

    auto data = parse_yahoo_chart_json(json);

    ASSERT_EQ(data.bars.size(), 1u);
    EXPECT_EQ(data.bars[0].date, "2024-01-02");
}

TEST(YahooParser, ReturnsEmptyOnApiError) {
    std::string json = R"({
        "chart": {
            "result": null,
            "error": {"code": "Not Found", "description": "No data found"}
        }
    })";

    EXPECT_TRUE(parse_yahoo_chart_json(json).bars.empty());
}

TEST(YahooParser, ReturnsEmptyOnInvalidJson) {
    EXPECT_TRUE(parse_yahoo_chart_json("this is not json").bars.empty());
}

TEST(YahooParser, ReturnsEmptyOnUnexpectedShape) {
    std::string json = R"({"chart": {"result": [{"unexpected": true}], "error": null}})";
    EXPECT_TRUE(parse_yahoo_chart_json(json).bars.empty());
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

    EXPECT_TRUE(parse_yahoo_chart_json(json).bars.empty());
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

    EXPECT_TRUE(parse_yahoo_chart_json(json).bars.empty());
}
