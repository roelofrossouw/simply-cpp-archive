cmake_minimum_required(VERSION 3.22)
set(SC_MODULE archive)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-archive" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_ARCHIVE_BUILD_TESTS "Build simply-cpp-archive tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.20.0)
# Codecs: deflate and gzip (zlib License), TODO: bzip2, xz and zstd. Formats: ZIP (libzip, BSD-3-Clause), TODO: tar.
find_package(ZLIB REQUIRED)
config_or_install_package(libzip libzip-dev libzip)
if (SC_ARCHIVE_LIBZIP_LIBRARY AND NOT EXISTS "${SC_ARCHIVE_LIBZIP_LIBRARY}")
    message(STATUS "libzip: the cached library location is gone, looking again")
    unset(SC_ARCHIVE_LIBZIP_LIBRARY CACHE)
endif ()
find_library(SC_ARCHIVE_LIBZIP_LIBRARY NAMES zip HINTS ${libzip_LIBRARY_DIRS} REQUIRED)

set(SOURCE_OBJECTS include/archive_format.h)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

add_sc_object(codec SOURCES src/zlib_streams.cpp src/zlib_streams.h
        LINK_LIBRARIES ZLIB::ZLIB
        PUBLIC_LINK_LIBRARIES ZLIB::ZLIB)
add_sc_object(codec_deflate)
add_sc_object(codec_gzip)
add_sc_object(archive_zip
        LINK_LIBRARIES PkgConfig::libzip
        PUBLIC_LINK_LIBRARIES ${SC_ARCHIVE_LIBZIP_LIBRARY})
add_sc_object(archive LINK_LIBRARIES sc::sc-core PUBLIC_LINK_LIBRARIES sc::sc-core)

add_sc_libraries(DESCRIPTION "simply-cpp archives (ZIP) and compression codecs (gzip, deflate)")
target_compile_features(sc-archive PUBLIC cxx_std_20)
target_compile_features(sc-archive-shared PUBLIC cxx_std_20)

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-archive
        COMPONENT development)

if (SC_ARCHIVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-archive-test)
endif ()

add_sc_example(sc-archive-demo)

package_sc_module(DEV_DEPENDS "simply-cpp-dev (>= 1.20.0~)" zlib1g-dev libzip-dev)
