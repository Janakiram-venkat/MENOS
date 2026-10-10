add_executable(menos_engines_tensor_network_tests
    contraction_test.cpp
    tensor_network_engine_test.cpp
)

target_link_libraries(menos_engines_tensor_network_tests PRIVATE
    menos_engines_tensor_network menos_engines_dense menos_analysis menos_ir GTest::gtest_main
)
target_compile_options(menos_engines_tensor_network_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_engines_tensor_network_tests)
