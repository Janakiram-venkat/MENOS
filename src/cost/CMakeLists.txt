add_library(menos_cost STATIC
    cost_model.cpp
    trajectory_count.cpp
)

target_include_directories(menos_cost PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(menos_cost PUBLIC menos_analysis menos_ir menos_common)
target_compile_features(menos_cost PUBLIC cxx_std_20)
target_compile_options(menos_cost PRIVATE ${MENOS_WARNING_FLAGS})
