# Only configured when MENOS_BUILD_PYTHON_BINDINGS=ON (root CMakeLists.txt).
# The dev/release presets used by scripts/verify.sh's C++-only stages leave
# this option OFF, so pure C++ development never needs Python or nanobind
# installed.
find_package(Python 3.14 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(menos_core_ext bindings.cpp)
target_link_libraries(menos_core_ext PRIVATE menos_api)
set_target_properties(menos_core_ext PROPERTIES OUTPUT_NAME "_core")

install(TARGETS menos_core_ext LIBRARY DESTINATION menos)
