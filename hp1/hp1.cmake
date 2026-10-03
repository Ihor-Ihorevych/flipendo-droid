# Harry Potter 1 port sources. Included from engine/CMakeLists.txt (one hp1_re: line) so that all
# HP1 code lives outside the SurrealEngine subtree and upstream merges never touch it.
file(GLOB_RECURSE HP1_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_LIST_DIR}/*.cpp
	${CMAKE_CURRENT_LIST_DIR}/*.h)
set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${HP1_SOURCES})
include_directories(${CMAKE_CURRENT_LIST_DIR})
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} PREFIX "hp1" FILES ${HP1_SOURCES})
