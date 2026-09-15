cmake_minimum_required(VERSION 3.21)
project(stocks_toolkit CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(data_layer
    src/data_layer/date_util.cpp
    src/data_layer/curl_http_client.cpp
    src/data_layer/yahoo_fetcher.cpp
    src/data_layer/yahoo_parser.cpp
    src/data_layer/price_store.cpp
)
target_include_directories(data_layer PUBLIC include)
target_link_libraries(data_layer PUBLIC CURL::libcurl unofficial::sqlite3::sqlite3
                                         nlohmann_json::nlohmann_json)

add_executable(stocks_toolkit_cli src/cli/main.cpp)
target_link_libraries(stocks_toolkit_cli PRIVATE data_layer)

enable_testing()
add_subdirectory(tests)
