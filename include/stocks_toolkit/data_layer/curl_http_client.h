#pragma once

#include "stocks_toolkit/data_layer/http_client.h"

namespace stocks_toolkit {

// Real HTTP client backed by libcurl. Throws HttpError on transport
// failure or a non-2xx response.
class CurlHttpClient : public IHttpClient {
public:
    std::string get(const std::string& url) override;
};

}  // namespace stocks_toolkit
