add_executable(menos_cli_tests
    golden_test.cpp
)

target_include_directories(menos_cli_tests PRIVATE ${CMAKE_SOURCE_DIR}/cli)
target_link_libraries(menos_cli_tests PRIVATE menos_cli_impl GTest::gtest_main)
target_compile_options(menos_cli_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_cli_tests)
