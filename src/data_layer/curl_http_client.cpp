#include "stocks_toolkit/data_layer/curl_http_client.h"

#include <curl/curl.h>

namespace stocks_toolkit {

namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// curl_easy_init() lazily calls curl_global_init() if it hasn't run yet, but
// libcurl documents that implicit init as NOT thread-safe. Doing our own
// init through a function-local static relies on C++11's guarantee that
// static local initialization is thread-safe and runs exactly once, so this
// stays safe even if multiple threads end up fetching concurrently later.
struct CurlGlobalInit {
    CurlGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInit() { curl_global_cleanup(); }
};

void ensure_curl_initialized() { static CurlGlobalInit global_init; }

}  // namespace

std::string CurlHttpClient::get(const std::string& url) {
    ensure_curl_initialized();

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw HttpError("curl_easy_init failed");
    }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "stocks-toolkit/0.1");

    CURLcode res = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw HttpError(std::string("curl request failed: ") + curl_easy_strerror(res));
    }
    if (status_code < 200 || status_code >= 300) {
        throw HttpError("HTTP request to " + url + " returned status " +
                         std::to_string(status_code));
    }

    return body;
}

}  // namespace stocks_toolkit
