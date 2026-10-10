add_library(menos_engines_mps STATIC
    mps_state.cpp
    apply_gate.cpp
    sampling.cpp
    expectation.cpp
    mps_engine.cpp
)

target_include_directories(menos_engines_mps PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_common: common::svd, the LAPACKE wrapper mps_state.cpp uses to
# move the orthogonality center. menos_analysis: compute_schmidt_rank_bound
# (MPSEngine::estimate()'s own feasibility check, the only caller --
# docs/BUILD_PLAN.md Phase 6). menos_ir: gate matrices via
# ir::generate_matrix, the same source every other engine uses.
target_link_libraries(menos_engines_mps PUBLIC menos_common menos_ir menos_analysis)
target_compile_features(menos_engines_mps PUBLIC cxx_std_20)
target_compile_options(menos_engines_mps PRIVATE ${MENOS_WARNING_FLAGS})
