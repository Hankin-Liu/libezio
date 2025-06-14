AUX_SOURCE_DIRECTORY(./data_struct LIB_SRCS)
AUX_SOURCE_DIRECTORY(./event LIB_SRCS)
AUX_SOURCE_DIRECTORY(./socket LIB_SRCS)
AUX_SOURCE_DIRECTORY(./thread LIB_SRCS)
AUX_SOURCE_DIRECTORY(./util LIB_SRCS)

# shared library
ADD_LIBRARY(ezio SHARED ${LIB_SRCS})

# static library
ADD_LIBRARY (ezio_static STATIC ${LIB_SRCS})
SET_TARGET_PROPERTIES(ezio_static PROPERTIES OUTPUT_NAME "ezio")
SET_TARGET_PROPERTIES(ezio PROPERTIES CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(ezio_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

# 设置 -fvisibility=hidden 选项
#SET_TARGET_PROPERTIES(StableEvent PROPERTIES
#    COMPILE_FLAGS "-fvisibility=hidden"
#    CXX_VISIBILITY_PRESET hidden
#)

#SET(OPENSSL_LIBS ${PROJECT_SOURCE_DIR}/lib/libcrypto.a)
#target_link_libraries(StableInfra PRIVATE ${OPENSSL_LIBS})
#target_link_libraries(StableInfra_static PRIVATE ${OPENSSL_LIBS})

#install command is executed when "make install"
INSTALL(TARGETS ezio DESTINATION /usr/local/lib)
INSTALL(TARGETS ezio_static DESTINATION /usr/local/lib)
INSTALL(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION /usr/local/include/libezio)
