set(GSL_PROJECT_DIR "${EXTERNAL_PROJECT_DIR}/GSL")

UpdateExternalLib("GSL" "https://github.com/microsoft/GSL.git" "1212beae777dba02c230ece8c0c0ec12790047ea")

set(GSL_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(${GSL_PROJECT_DIR} "${CMAKE_CURRENT_BINARY_DIR}/GSL" EXCLUDE_FROM_ALL)
