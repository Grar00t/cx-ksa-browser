if(NOT DEFINED TEST_FILE OR NOT DEFINED TEST_EXE)
  message(FATAL_ERROR "TEST_FILE and TEST_EXE are required")
endif()

file(TO_CMAKE_PATH "${TEST_EXE}" TEST_EXE_CMAKE)
file(WRITE "${TEST_FILE}"
  "add_test(\"storage_tests\" \"${TEST_EXE_CMAKE}\")\n")
