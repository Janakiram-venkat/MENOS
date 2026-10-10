# Readout-error mitigation arithmetic (Phase 14, docs/adr/0018 sections 3-4):
# pure functions on dense vectors, with no circuit execution.
add_library(menos_mitigation STATIC
    readout_mitigation.cpp
)

target_include_directories(menos_mitigation PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(menos_mitigation PUBLIC menos_common menos_ir)
target_compile_features(menos_mitigation PUBLIC cxx_std_20)
target_compile_options(menos_mitigation PRIVATE ${MENOS_WARNING_FLAGS})
