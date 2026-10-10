# docs/BUILD_PLAN.md Phase 7: linked only to the public API
# (menos_api, menos_qasm2), no engine or planner internals directly.
# Split into a small library (cli_impl, parameterized on ostream) plus a
# thin main.cpp so tests/cli can exercise the exact same command-
# dispatch and formatting logic in process, without spawning a
# subprocess.
add_library(menos_cli_impl STATIC
    cli_impl.cpp
)
target_link_libraries(menos_cli_impl PUBLIC menos_api menos_qasm2 menos_common)
target_compile_features(menos_cli_impl PUBLIC cxx_std_20)
target_compile_options(menos_cli_impl PRIVATE ${MENOS_WARNING_FLAGS})

add_executable(menos_cli
    main.cpp
)
target_link_libraries(menos_cli PRIVATE menos_cli_impl)
target_compile_features(menos_cli PRIVATE cxx_std_20)
target_compile_options(menos_cli PRIVATE ${MENOS_WARNING_FLAGS})
set_target_properties(menos_cli PROPERTIES OUTPUT_NAME menos)
