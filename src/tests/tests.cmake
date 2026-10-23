# FlipendoTests: unit tests for code that needs no game data (src/tests/Test.h). Included from ../flipendo.cmake,
# before engine/CMakeLists.txt defines SurrealCommon (the link is resolved when the build files are generated).
# Run them with `ctest --test-dir <build>` (Visual Studio: add `-C Release`) or the executable itself.
file(GLOB FLIPENDO_TEST_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_LIST_DIR}/*.cpp
	${CMAKE_CURRENT_LIST_DIR}/*.h)
add_executable(FlipendoTests ${FLIPENDO_TEST_SOURCES})
target_link_libraries(FlipendoTests SurrealCommon ${SURREALCOMMON_LIBS})
set_target_properties(FlipendoTests PROPERTIES CXX_STANDARD 20)
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} PREFIX "tests" FILES ${FLIPENDO_TEST_SOURCES})

enable_testing()
add_test(NAME FlipendoTests COMMAND FlipendoTests)
