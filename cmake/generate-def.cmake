# generate-def.cmake
# Generates fyaml.def from libfyaml.h by extracting FY_EXPORT functions
#
# Usage: cmake -DINPUT_HEADER=path/to/libfyaml.h -DOUTPUT_DEF=path/to/fyaml.def -P generate-def.cmake

if(NOT INPUT_HEADER)
    message(FATAL_ERROR "INPUT_HEADER not specified")
endif()

if(NOT OUTPUT_DEF)
    message(FATAL_ERROR "OUTPUT_DEF not specified")
endif()

# Functions declared with FY_EXPORT but not implemented as standalone functions
# (they may be inline, macros, or not implemented)
# These are determined by linker errors - if the symbol is unresolved, add it here
set(EXCLUDE_FUNCTIONS)

# Read the header file
file(READ "${INPUT_HEADER}" HEADER_CONTENT)

# Find all function names that have FY_EXPORT
# Pattern: function_name(...) followed by FY_EXPORT on same or next line
# There may be FY_FORMAT(...) or other attributes between function and FY_EXPORT
# We look for: fy_ word( ... ) whitespace [optional FY_FORMAT(...)] whitespace FY_EXPORT
string(REGEX MATCHALL "fy_[a-z_][a-z0-9_]*[ \t]*\\([^)]*\\)[\r\n\t ]+(FY_FORMAT\\([^)]*\\)[\r\n\t ]+)?FY_EXPORT" MATCHES "${HEADER_CONTENT}")

# Extract just the function names
set(FUNCTION_NAMES "")
foreach(MATCH ${MATCHES})
    # Extract function name (everything before the opening paren)
    string(REGEX REPLACE "[ \t]*\\(.*" "" FUNC_NAME "${MATCH}")
    list(APPEND FUNCTION_NAMES "${FUNC_NAME}")
endforeach()

# Remove duplicates and sort
list(REMOVE_DUPLICATES FUNCTION_NAMES)
list(SORT FUNCTION_NAMES)

# Remove excluded functions
foreach(EXCLUDE ${EXCLUDE_FUNCTIONS})
    list(REMOVE_ITEM FUNCTION_NAMES "${EXCLUDE}")
endforeach()

# Generate the .def file content
set(DEF_CONTENT "; fyaml.def - Auto-generated module definition file for fyaml.dll
; Generated from libfyaml.h - DO NOT EDIT MANUALLY
;
; This file exports the public API symbols marked with FY_EXPORT.
; Regenerate with: cmake -DINPUT_HEADER=include/libfyaml.h -DOUTPUT_DEF=src/lib/fyaml.def -P cmake/generate-def.cmake

LIBRARY fyaml
EXPORTS
")

foreach(FUNC ${FUNCTION_NAMES})
    string(APPEND DEF_CONTENT "    ${FUNC}\n")
endforeach()

# Write the output file
file(WRITE "${OUTPUT_DEF}" "${DEF_CONTENT}")

# Report
list(LENGTH FUNCTION_NAMES NUM_FUNCS)
message(STATUS "Generated ${OUTPUT_DEF} with ${NUM_FUNCS} exported functions")
