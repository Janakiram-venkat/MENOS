add_library(menos_engines_pauli STATIC
    pauli_propagation_engine.cpp
)

target_include_directories(menos_engines_pauli PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_ir: PauliPropagationEngine dispatches GateOp opcodes via
# gate_info().is_clifford, the same way StabilizerEngine does.
target_link_libraries(menos_engines_pauli PUBLIC menos_common menos_ir)
target_compile_features(menos_engines_pauli PUBLIC cxx_std_20)
target_compile_options(menos_engines_pauli PRIVATE ${MENOS_WARNING_FLAGS})
