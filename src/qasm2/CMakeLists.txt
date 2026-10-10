add_library(menos_qasm2 STATIC
    parser.cpp
)

target_include_directories(menos_qasm2 PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(menos_qasm2 PUBLIC menos_ir menos_common)
target_compile_features(menos_qasm2 PUBLIC cxx_std_20)
target_compile_options(menos_qasm2 PRIVATE ${MENOS_WARNING_FLAGS})
