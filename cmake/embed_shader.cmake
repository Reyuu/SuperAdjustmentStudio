if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "INPUT and OUTPUT are required")
endif()

# Batch files pass -D values with literal quotes; strip them.
string(REGEX REPLACE "^\"(.*)\"$" "\\1" INPUT "${INPUT}")
string(REGEX REPLACE "^\"(.*)\"$" "\\1" OUTPUT "${OUTPUT}")

get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(READ "${INPUT}" SHADER_SOURCE)
file(WRITE "${OUTPUT}" "R\"hlsl(\n${SHADER_SOURCE}\n)hlsl\";\n")
