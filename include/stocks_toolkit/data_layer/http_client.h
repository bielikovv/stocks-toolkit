#pragma once

#include <stdexcept>
#include <string>

namespace stocks_toolkit {

class HttpError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Abstraction over "fetch this URL, get the body back" so the fetch logic
// can be tested offline with a fake implementation instead of hitting the
// network in every test run.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual std::string get(const std::string& url) = 0;
};

}  // namespace stocks_toolkit
