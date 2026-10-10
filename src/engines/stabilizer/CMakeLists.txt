add_library(menos_engines_stabilizer STATIC
    tableau.cpp
    stabilizer_engine.cpp
)

target_include_directories(menos_engines_stabilizer PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_ir: StabilizerEngine dispatches GateOp opcodes via gate_info()
# .is_clifford and its own opcode->Tableau-method mapping (Tableau
# itself stays decoupled from KIR, see tableau.h).
target_link_libraries(menos_engines_stabilizer PUBLIC menos_common menos_ir)
target_compile_features(menos_engines_stabilizer PUBLIC cxx_std_20)
target_compile_options(menos_engines_stabilizer PRIVATE ${MENOS_WARNING_FLAGS})
