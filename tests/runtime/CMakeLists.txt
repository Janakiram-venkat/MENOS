add_executable(menos_runtime_tests
    runtime_test.cpp
    recombine_statevector_test.cpp
    combine_counts_test.cpp
    check_plan_invariants_test.cpp
)

target_link_libraries(menos_runtime_tests PRIVATE menos_runtime menos_plan menos_engines_dense
                                                  GTest::gtest_main)
target_compile_options(menos_runtime_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_runtime_tests)
