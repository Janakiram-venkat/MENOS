add_library(menos_api STATIC
    menos.cpp
    batch.cpp
    mitigation.cpp
    replan_controller.cpp
)

target_include_directories(menos_api PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(menos_api PUBLIC menos_ir menos_plan menos_runtime menos_mitigation menos_common)
target_compile_features(menos_api PUBLIC cxx_std_20)
target_compile_options(menos_api PRIVATE ${MENOS_WARNING_FLAGS})
