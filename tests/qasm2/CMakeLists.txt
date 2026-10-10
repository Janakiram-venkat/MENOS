add_executable(menos_qasm2_tests
    parser_test.cpp
    round_trip_test.cpp
)

target_link_libraries(menos_qasm2_tests PRIVATE menos_qasm2 menos_api menos_test_fixture_loader
                                                GTest::gtest_main)
target_compile_options(menos_qasm2_tests PRIVATE ${MENOS_WARNING_FLAGS})
target_compile_definitions(menos_qasm2_tests PRIVATE MENOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(menos_qasm2_tests)
