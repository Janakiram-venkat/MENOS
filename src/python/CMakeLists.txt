# Only configured when MENOS_BUILD_PYTHON_BINDINGS=ON (root CMakeLists.txt).
# The dev/release presets used by scripts/verify.sh's C++-only stages leave
# this option OFF, so pure C++ development never needs Python or nanobind
# installed.
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(menos_core_ext bindings.cpp)
target_link_libraries(menos_core_ext PRIVATE menos_api menos_qasm3)
set_target_properties(menos_core_ext PROPERTIES OUTPUT_NAME "_core")

install(TARGETS menos_core_ext LIBRARY DESTINATION menos)

# Windows: Python searches an extension module's own directory for the DLLs
# it imports, so OpenBLAS ships beside _core.pyd, with its licence
# (docs/adr/0022 section 5).
if(WIN32)
    install(FILES "${MENOS_OPENBLAS_DLL}" DESTINATION menos)
    install(FILES "${MENOS_OPENBLAS_ROOT}/LICENSE" DESTINATION menos RENAME OPENBLAS_LICENSE)
endif()
