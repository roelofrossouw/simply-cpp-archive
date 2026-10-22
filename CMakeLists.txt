cmake_minimum_required(VERSION 3.22)
set(SC_MODULE compression)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-compression" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_COMPRESSION_BUILD_TESTS "Build simply-cpp-compression tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.20.0)
# deflate and gzip (zlib License); ZIP archives (libzip, BSD-3-Clause). TODO: bzip2, xz and zstd.
find_package(ZLIB REQUIRED)
config_or_install_package(libzip libzip-dev libzip)
if (SC_COMPRESSION_LIBZIP_LIBRARY AND NOT EXISTS "${SC_COMPRESSION_LIBZIP_LIBRARY}")
    message(STATUS "libzip: the cached library location is gone, looking again")
    unset(SC_COMPRESSION_LIBZIP_LIBRARY CACHE)
endif ()
find_library(SC_COMPRESSION_LIBZIP_LIBRARY NAMES zip HINTS ${libzip_LIBRARY_DIRS} REQUIRED)

set(SOURCE_OBJECTS include/compression.h)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

add_sc_object(compression
        LINK_LIBRARIES sc::sc-core ZLIB::ZLIB PkgConfig::libzip
        PUBLIC_LINK_LIBRARIES sc::sc-core ZLIB::ZLIB ${SC_COMPRESSION_LIBZIP_LIBRARY})

add_sc_libraries(DESCRIPTION "simply-cpp ZIP archives and gzip/deflate streams")
target_compile_features(sc-compression PUBLIC cxx_std_20)
target_compile_features(sc-compression-shared PUBLIC cxx_std_20)

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-compression
        COMPONENT development)

if (SC_COMPRESSION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-compression-test)
endif ()

add_sc_example(sc-compression-demo)

package_sc_module(DEV_DEPENDS "simply-cpp-dev (>= 1.20.0~)" zlib1g-dev libzip-dev)
