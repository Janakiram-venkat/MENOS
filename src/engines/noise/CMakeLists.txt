# Noise helpers shared by the engines that run noisy jobs: the density-matrix
# engine (docs/adr/0016) and the trajectory engine (docs/adr/0017).
add_library(menos_engines_noise STATIC
    readout.cpp
)

target_include_directories(menos_engines_noise PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(menos_engines_noise PUBLIC menos_common menos_ir)
target_compile_features(menos_engines_noise PUBLIC cxx_std_20)
target_compile_options(menos_engines_noise PRIVATE ${MENOS_WARNING_FLAGS})
