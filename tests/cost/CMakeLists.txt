add_executable(menos_cost_tests
    cost_model_test.cpp
    trajectory_count_test.cpp
)

target_link_libraries(menos_cost_tests PRIVATE menos_cost menos_engines_dense menos_engines_stabilizer
                                               GTest::gtest_main)
target_compile_options(menos_cost_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_cost_tests)
