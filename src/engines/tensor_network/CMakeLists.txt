add_library(menos_engines_tensor_network STATIC
    contraction.cpp
    tensor_network_engine.cpp
)

target_include_directories(menos_engines_tensor_network PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_analysis: the closed-network graph/timeline builder and the
# min-fill elimination heuristic (docs/BUILD_PLAN.md Phase 8). menos_ir:
# gate matrices via ir::generate_matrix, the same source every other
# engine uses. menos_common: PauliString bit helpers and
# common::pauli_matrix_2x2.
target_link_libraries(menos_engines_tensor_network PUBLIC menos_common menos_ir menos_analysis)
target_compile_features(menos_engines_tensor_network PUBLIC cxx_std_20)
target_compile_options(menos_engines_tensor_network PRIVATE ${MENOS_WARNING_FLAGS})
