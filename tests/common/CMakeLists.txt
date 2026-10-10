add_executable(menos_common_tests
    error_test.cpp
    checked_math_test.cpp
    bitstring_test.cpp
    splitmix64_test.cpp
    rng_test.cpp
    pauli_row_test.cpp
    svd_test.cpp
)

target_link_libraries(menos_common_tests PRIVATE menos_common GTest::gtest_main)
target_compile_options(menos_common_tests PRIVATE ${MENOS_WARNING_FLAGS})

include(GoogleTest)
gtest_discover_tests(menos_common_tests)
