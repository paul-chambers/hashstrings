cmake_minimum_required(VERSION 3.10)
project(HashStrings)

set( CMAKE_C_STANDARD 11 )
set( CMAKE_C_FLAGS    "-Wall -Wextra -Wpedantic" )
set( CMAKE_INSTALL_PREFIX /usr )

include_directories(.)

add_library( argtable3 argtable3.c argtable3.h )

add_executable( hashstrings
                hashstrings.c    hashstrings.h
                btree/btree.c    btree/btree.h
                libhashstrings.c libhashstrings.h )

target_link_libraries( hashstrings argtable3 config m )

install( TARGETS hashstrings
         RUNTIME DESTINATION /usr/bin )


add_library( libhashstrings SHARED
        libhashstrings.h
        libhashstrings.c)

set_target_properties( libhashstrings
        PROPERTIES
        PUBLIC_HEADER libhashstrings.h
        LIBRARY_OUTPUT_NAME hashstrings)

install(TARGETS libhashstrings
        LIBRARY PUBLIC_HEADER)
