# KnowWonder engine sources (shared by the Harry Potter games). Included from ../flipendo.cmake.
file(GLOB_RECURSE KW_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_LIST_DIR}/*.cpp
	${CMAKE_CURRENT_LIST_DIR}/*.h)
set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${KW_SOURCES})
include_directories(${CMAKE_CURRENT_LIST_DIR})
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} PREFIX "kw" FILES ${KW_SOURCES})
