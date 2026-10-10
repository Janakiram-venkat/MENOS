add_library(menos_runtime STATIC
    runtime.cpp
    result_impl.cpp
    check_plan_invariants.cpp
    recombine_statevector.cpp
    combine_counts.cpp
    combine_expectation.cpp
    recombine_cut.cpp
)

target_include_directories(menos_runtime PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_engines_dense/menos_engines_stabilizer are no longer linked here
# directly (Phase 4): the runtime dispatches through
# plan::available_engines() and the abstract SimulationEngine interface
# only, never a concrete engine header.
target_link_libraries(menos_runtime PUBLIC menos_ir menos_plan menos_common)
target_compile_features(menos_runtime PUBLIC cxx_std_20)
target_compile_options(menos_runtime PRIVATE ${MENOS_WARNING_FLAGS})
