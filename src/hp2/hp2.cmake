# Harry Potter 2 specific sources (HP2-only natives, HP2 bytecode). Included from ../flipendo.cmake.
file(GLOB_RECURSE HP2_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_LIST_DIR}/*.cpp
	${CMAKE_CURRENT_LIST_DIR}/*.h)
set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${HP2_SOURCES})
include_directories(${CMAKE_CURRENT_LIST_DIR})
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} PREFIX "hp2" FILES ${HP2_SOURCES})
