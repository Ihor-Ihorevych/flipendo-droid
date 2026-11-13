# Flipendo's own code, outside the SurrealEngine subtree so upstream merges never touch it. Included from
# engine/CMakeLists.txt (one flipendo: line).
# Flipendo's version, shown in the window title and the log: -DFLIPENDO_VERSION=<tag> (the release workflow passes
# the tag), else git describe of this repository (v1.1-17-g9c0a8ed between releases), else "dev". Without the
# leading v. Read when CMake configures; a commit or checkout reconfigures through .git/logs/HEAD.
set(FLIPENDO_VERSION "" CACHE STRING "Flipendo version (release tag); empty = git describe")
set(FLIPENDO_VERSION_STRING "${FLIPENDO_VERSION}")
if(NOT FLIPENDO_VERSION_STRING)
	find_package(Git QUIET)
	if(GIT_FOUND)
		execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
			WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/.. OUTPUT_VARIABLE FLIPENDO_VERSION_STRING
			OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	endif()
	if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../.git/logs/HEAD)
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../.git/logs/HEAD)
	endif()
endif()
if(NOT FLIPENDO_VERSION_STRING)
	set(FLIPENDO_VERSION_STRING "dev")
endif()
string(REGEX REPLACE "^v([0-9])" "\\1" FLIPENDO_VERSION_STRING "${FLIPENDO_VERSION_STRING}")
message(STATUS "Flipendo version: ${FLIPENDO_VERSION_STRING}")

include(${CMAKE_CURRENT_LIST_DIR}/knowwonder/knowwonder.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/hp1/hp1.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/hp2/hp2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/tests.cmake)
