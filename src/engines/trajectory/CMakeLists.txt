add_library(menos_engines_trajectory STATIC
    trajectory_engine.cpp
)

target_include_directories(menos_engines_trajectory PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_engines_dense: psi is a dense::StateVector evolved by dense::apply_matrix,
# shots are drawn through dense::CumulativeDistribution, and expectation terms use
# the dense CPU kernel -- the reuse docs/adr/0017 section 2 specifies.
# menos_engines_noise: readout errors, shared with the density-matrix engine.
target_link_libraries(menos_engines_trajectory PUBLIC menos_common menos_ir menos_engines_dense
                                                       menos_engines_noise menos_kernels_cpu)
target_compile_features(menos_engines_trajectory PUBLIC cxx_std_20)
target_compile_options(menos_engines_trajectory PRIVATE ${MENOS_WARNING_FLAGS})
