# Phase 6: svd.cpp wraps LAPACKE_zgesdd. CMake's own FindLAPACK module
# does not know about LAPACKE (checked the installed
# /usr/share/cmake-*/Modules/FindLAPACK.cmake -- no mention of it), so
# the header and library are located manually, matching the working
# approach verified in a scratch probe before this file was written.
find_package(LAPACK REQUIRED)

find_path(
    LAPACKE_INCLUDE_DIR
    NAMES lapacke.h
    PATHS
        /usr/local/include
        /usr/include
        /usr/include/lapacke
    REQUIRED
)
find_library(LAPACKE_LIBRARY NAMES lapacke REQUIRED)

add_library(menos_common STATIC
    bitstring.cpp
    error.cpp
    rng.cpp
    pauli_row.cpp
    observable_restrict.cpp
    svd.cpp
)

target_include_directories(menos_common PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
                                         PRIVATE ${LAPACKE_INCLUDE_DIR})
target_compile_features(menos_common PUBLIC cxx_std_20)
target_compile_options(menos_common PRIVATE ${MENOS_WARNING_FLAGS})
target_link_libraries(menos_common PUBLIC LAPACK::LAPACK ${LAPACKE_LIBRARY})
