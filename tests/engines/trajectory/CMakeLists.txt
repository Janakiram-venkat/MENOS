add_executable(menos_engines_trajectory_tests
    trajectory_engine_test.cpp
)

target_link_libraries(menos_engines_trajectory_tests PRIVATE menos_engines_trajectory menos_ir
                                                             GTest::gtest_main)
target_compile_options(menos_engines_trajectory_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_engines_trajectory_tests)
