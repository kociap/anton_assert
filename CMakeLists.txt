cmake_minimum_required(VERSION 3.12)

project(anton_assert CXX)

add_library(anton_assert
    "${CMAKE_CURRENT_SOURCE_DIR}/include/anton_assert.hpp"
)

set_target_properties(anton_assert
    PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
)

if(WIN32)
    target_sources(anton_assert
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/anton_assert_win.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debugging.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debugging.cpp"
    )

    target_link_libraries(anton_assert
        DbgHelp
    )
else()
    target_sources(anton_assert
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/anton_assert_posix.cpp"
    )    
endif()

target_include_directories(anton_assert
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
