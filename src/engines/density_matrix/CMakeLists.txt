add_library(menos_engines_density_matrix STATIC
    density_matrix_engine.cpp
)

target_include_directories(menos_engines_density_matrix PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
# menos_engines_dense: rho lives in dense::StateVector and is evolved by
# dense::apply_matrix; counts reuse dense::sample and CumulativeDistribution over
# rho's diagonal (docs/adr/0016 section 4) -- the same reuse the CUDA dense engine
# already makes of the dense engine's sampling.
target_link_libraries(menos_engines_density_matrix PUBLIC menos_common menos_ir menos_engines_dense
                                                           menos_engines_noise)
target_compile_features(menos_engines_density_matrix PUBLIC cxx_std_20)
target_compile_options(menos_engines_density_matrix PRIVATE ${MENOS_WARNING_FLAGS})
