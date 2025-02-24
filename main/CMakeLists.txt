FILE(GLOB srcFiles *.c)



idf_component_register(SRCS ${srcFiles}
                        INCLUDE_DIRS ${SRC_INCLUDE_DIRS}    "."
                        PRIV_REQUIRES  )