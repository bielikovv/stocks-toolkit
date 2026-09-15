find_package(GTest CONFIG REQUIRED)

add_executable(data_layer_tests
    yahoo_parser_test.cpp
    price_store_test.cpp
    yahoo_fetcher_test.cpp
    date_util_test.cpp
)
target_link_libraries(data_layer_tests PRIVATE data_layer GTest::gtest GTest::gtest_main
                                                GTest::gmock)

include(GoogleTest)
gtest_discover_tests(data_layer_tests)
