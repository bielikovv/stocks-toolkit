#include "stocks_toolkit/data_layer/yahoo_fetcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "stocks_toolkit/data_layer/http_client.h"

using stocks_toolkit::HttpError;
using stocks_toolkit::IHttpClient;
using stocks_toolkit::YahooFetcher;
using ::testing::_;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::Throw;

namespace {

class MockHttpClient : public IHttpClient {
public:
    MOCK_METHOD(std::string, get, (const std::string& url), (override));
};

constexpr const char* kSampleResponse = R"({
    "chart": {
        "result": [{
            "timestamp": [1704153600],
            "indicators": {
                "quote": [{"open": [1.0], "high": [2.0], "low": [0.5], "close": [1.5],
                           "volume": [100]}]
            }
        }],
        "error": null
    }
})";

}  // namespace

TEST(YahooFetcher, FetchFullHistoryRequestsTickerWithPeriod1Zero) {
    MockHttpClient client;
    EXPECT_CALL(client, get(AllOf(HasSubstr("/v8/finance/chart/NVDA"), HasSubstr("period1=0"),
                                    HasSubstr("interval=1d"), HasSubstr("events=div,splits"))))
        .WillOnce(Return(kSampleResponse));

    YahooFetcher fetcher(client);
    auto data = fetcher.fetch_full_history("NVDA");

    ASSERT_EQ(data.bars.size(), 1u);
    EXPECT_EQ(data.bars[0].date, "2024-01-02");
}

TEST(YahooFetcher, FetchSinceUsesGivenDateAsPeriod1) {
    MockHttpClient client;
    // 2024-01-02 -> unix 1704153600 (verified in date_util_test).
    EXPECT_CALL(client, get(HasSubstr("period1=1704153600"))).WillOnce(Return(kSampleResponse));

    YahooFetcher fetcher(client);
    auto data = fetcher.fetch_since("NVDA", "2024-01-02");

    ASSERT_EQ(data.bars.size(), 1u);
}

TEST(YahooFetcher, PropagatesHttpErrors) {
    MockHttpClient client;
    EXPECT_CALL(client, get(_)).WillOnce(Throw(HttpError("network down")));

    YahooFetcher fetcher(client);
    EXPECT_THROW(fetcher.fetch_full_history("NVDA"), HttpError);
}
