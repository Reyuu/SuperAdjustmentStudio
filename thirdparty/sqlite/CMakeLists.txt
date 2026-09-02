cmake_minimum_required(VERSION 3.15)
project(SQLite)

add_library(sqlite STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c
)

target_include_directories(sqlite PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(sqlite PRIVATE
    SQLITE_OS_WIN=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_AUTOINIT
)

if (MSVC)
    target_compile_options(sqlite PRIVATE /W0)
endif()
