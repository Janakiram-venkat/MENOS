add_executable(menos_mitigation_tests
    readout_mitigation_test.cpp
)

target_link_libraries(menos_mitigation_tests PRIVATE menos_mitigation GTest::gtest_main)
target_compile_options(menos_mitigation_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_mitigation_tests)
