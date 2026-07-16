CMAKE_MINIMUM_REQUIRED(VERSION 3.14)
PROJECT(ezio)
unset(ASAN_SWITCH CACHE)
OPTION(ASAN_SWITCH "use asan tool" OFF)

# ============================================================================
# Compiler & architecture detection
# ============================================================================
SET(CMAKE_CXX_COMPILER "g++")

# Base C++ standard: everything except coroutine sources uses C++11.
# We set this as a per-target requirement rather than global CMAKE_CXX_FLAGS,
# so coroutine sources can independently use C++20 without conflict.
SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -Wno-unused-parameter -Wno-unused-function -Wl,-Bsymbolic-functions -Wno-builtin-macro-redefined -Wl,--exclude-libs,ALL")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -g3 -ggdb ")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O2 -g")
#SET(CMAKE_BUILD_TYPE "Release")
SET(CMAKE_BUILD_TYPE "Debug")
#ADD_DEFINITIONS(-fvisibility=hidden)
#ADD_DEFINITIONS(-D_GLIBCXX_USE_CXX11_ABI=1)

IF(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -m64")
ELSEIF(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -mabi=lp64 -fsigned-char")
ELSE()
    SET(CMAKE_CXX_FLAGS  "")
ENDIF()

IF(ASAN_SWITCH)
    message(STATUS "ASAN_SWITCH ON.")
    SET(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer ${CMAKE_CXX_FLAGS}")
ENDIF()

# ============================================================================
# C++20 coroutine support detection
# ============================================================================
include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)

# Check if <coroutine> header exists (C++20 signature)
check_include_file_cxx(coroutine EZIO_HAS_COROUTINE_HEADER)

# GCC needs -fcoroutines flag before C++23; Clang supports <coroutine> >= 14
check_cxx_compiler_flag("-std=c++20" EZIO_CAN_CXX20)
if(EZIO_CAN_CXX20 AND EZIO_HAS_COROUTINE_HEADER)
    set(EZIO_HAS_COROUTINE ON)
    add_compile_definitions(EZIO_ENABLE_COROUTINE=1)
    message(STATUS "C++20 coroutine support: YES  →  coroutine library will be built")
else()
    set(EZIO_HAS_COROUTINE OFF)
    add_compile_definitions(EZIO_ENABLE_COROUTINE=0)
    message(STATUS "C++20 coroutine support: NO   →  coroutine library will be SKIPPED")
    if(EZIO_CAN_CXX20 AND NOT EZIO_HAS_COROUTINE_HEADER)
        message(STATUS "  Reason: <coroutine> header not found (check compiler version)")
    elseif(NOT EZIO_CAN_CXX20)
        message(STATUS "  Reason: compiler does not support -std=c++20 (need GCC >= 10 or Clang >= 14)")
    endif()
endif()

SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_VERBOSE_MAKEFILE ON)

MESSAGE("++++++++++++++Start Build+++++++++++++++++")

# include directories
INCLUDE_DIRECTORIES(
    ${PROJECT_SOURCE_DIR}/include
)

ADD_SUBDIRECTORY(source)
