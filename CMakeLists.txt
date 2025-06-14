CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(ezio)
unset(ASAN_SWITCH CACHE)
OPTION(ASAN_SWITCH "use asan tool" OFF)

SET(CMAKE_CXX_COMPILER "g++")
SET(CMAKE_CXX_FLAGS "-fPIC -std=c++11 -Wall -Wno-unused-parameter -Wno-unused-function -Wl,-Bsymbolic-functions -Wno-builtin-macro-redefined -Wl,--exclude-libs,ALL")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -g3 -ggdb ")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O2 -g")
#SET(CMAKE_BUILD_TYPE "Release")
SET(CMAKE_BUILD_TYPE "Debug")
#ADD_DEFINITIONS(-fvisibility=hidden)
#ADD_DEFINITIONS(-D_GLIBCXX_USE_CXX11_ABI=1)
IF(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -m64")
ELSEIF(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS}-mabi=lp64 -fsigned-char")
ELSE()
    SET(CMAKE_CXX_FLAGS  "")
ENDIF()

IF(ASAN_SWITCH)
    message(STATUS "ASAN_SWITCH ON.")
    SET(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer ${CMAKE_CXX_FLAGS}")
ENDIF()

SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_VERBOSE_MAKEFILE ON)

MESSAGE("++++++++++++++Start Build+++++++++++++++++")

# include directories
INCLUDE_DIRECTORIES(
    ${PROJECT_SOURCE_DIR}/include
)

ADD_SUBDIRECTORY(source)
