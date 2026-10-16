if(NOT DEFINED TEST_EXECUTABLE OR NOT EXISTS "${TEST_EXECUTABLE}")
	message(FATAL_ERROR "The termination check requires its built fixture executable")
endif()

execute_process(COMMAND "${TEST_EXECUTABLE}" --exit-child RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${result}" STREQUAL "0" OR NOT "${output}" STREQUAL "configured-exit-before\n" OR NOT "${errors}" STREQUAL "")
	message(FATAL_ERROR "Configured exit fixture failed: status=${result}; stdout=${output}; stderr=${errors}")
endif()
