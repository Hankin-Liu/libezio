# ============================================================================
# Base sources (C++11)
# ============================================================================
AUX_SOURCE_DIRECTORY(./data_struct BASE_SRCS)
AUX_SOURCE_DIRECTORY(./event BASE_SRCS)
AUX_SOURCE_DIRECTORY(./socket BASE_SRCS)
AUX_SOURCE_DIRECTORY(./thread BASE_SRCS)
AUX_SOURCE_DIRECTORY(./util BASE_SRCS)

set(LIB_SRCS ${BASE_SRCS})

# ============================================================================
# Coroutine sources (C++20) — only if compiler supports it
# ============================================================================
if(EZIO_HAS_COROUTINE)
    AUX_SOURCE_DIRECTORY(./coroutine CORO_SRCS)

    # These files require C++20 for std::coroutine_handle / std::noop_coroutine
    set_source_files_properties(${CORO_SRCS} PROPERTIES
        COMPILE_OPTIONS "-std=c++20;$<$<CXX_COMPILER_ID:GNU>:-fcoroutines>"
    )

    list(APPEND LIB_SRCS ${CORO_SRCS})
    message(STATUS "Coroutine sources included: ${CORO_SRCS}")
else()
    message(STATUS "Coroutine sources SKIPPED (C++20 not available)")
endif()

# ============================================================================
# shared library
# ============================================================================
ADD_LIBRARY(ezio SHARED ${LIB_SRCS})

# ============================================================================
# static library
# ============================================================================
ADD_LIBRARY(ezio_static STATIC ${LIB_SRCS})
SET_TARGET_PROPERTIES(ezio_static PROPERTIES OUTPUT_NAME "ezio")
SET_TARGET_PROPERTIES(ezio PROPERTIES CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(ezio_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

# ============================================================================
# install
# ============================================================================
INSTALL(TARGETS ezio DESTINATION /usr/local/lib)
INSTALL(TARGETS ezio_static DESTINATION /usr/local/lib)
INSTALL(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION /usr/local/include/libezio)
