CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(test_program)
unset(ASAN_SWITCH CACHE)
OPTION(ASAN_SWITCH   "use asan tool"  ON)

SET(CMAKE_CXX_COMPILER "g++")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O2 -g -Wall")
#SET(CMAKE_BUILD_TYPE "Release")
SET(CMAKE_BUILD_TYPE "Debug")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_definitions(-std=c++11)
IF( ASAN_SWITCH )
    SET(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer ${CMAKE_CXX_FLAGS}")
ENDIF()

LINK_LIBRARIES("${PROJECT_SOURCE_DIR}/../../../../../lib/libezio.so")

AUX_SOURCE_DIRECTORY(src SOURCE_FILES)

INCLUDE_DIRECTORIES(../../../../../include)

ADD_EXECUTABLE(test_program ${SOURCE_FILES})

TARGET_LINK_LIBRARIES(test_program
-lpthread
dl
uring
#asan
)
