if(NOT DEFINED PYTHON_EXECUTABLE OR NOT DEFINED GENERATOR OR
   NOT DEFINED TRACKED_TABLE OR NOT DEFINED CHECK_ROOT)
  message(FATAL_ERROR "generated transform table check is missing an input")
endif()

set(GENERATED_ROOT "${CHECK_ROOT}/generated-transform-table")
file(REMOVE_RECURSE "${GENERATED_ROOT}")
file(MAKE_DIRECTORY "${GENERATED_ROOT}/src/logic")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${GENERATOR}"
  WORKING_DIRECTORY "${GENERATED_ROOT}"
  RESULT_VARIABLE GENERATE_RESULT
  OUTPUT_VARIABLE GENERATE_OUTPUT
  ERROR_VARIABLE GENERATE_ERROR
)

if(NOT GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR "transform table generation failed: ${GENERATE_ERROR}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${GENERATED_ROOT}/src/logic/transform_table.inc"
          "${TRACKED_TABLE}"
  RESULT_VARIABLE COMPARE_RESULT
)

if(NOT COMPARE_RESULT EQUAL 0)
  message(FATAL_ERROR "tracked transform table differs from generated output")
endif()

message(STATUS "Generated transform table matches byte for byte")
