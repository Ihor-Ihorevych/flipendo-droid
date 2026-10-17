# KnowWonder engine sources (shared by the Harry Potter games). Included from ../flipendo.cmake.
file(GLOB_RECURSE KW_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_LIST_DIR}/*.cpp
	${CMAKE_CURRENT_LIST_DIR}/*.h)
set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${KW_SOURCES})
include_directories(${CMAKE_CURRENT_LIST_DIR})
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} PREFIX "knowwonder" FILES ${KW_SOURCES})
# KWNativeFont.cpp rasterizes system fonts with SurrealWidgets' TrueType reader (its headers are internal to it)
set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/KWNativeFont.cpp APPEND PROPERTY INCLUDE_DIRECTORIES
	${CMAKE_CURRENT_LIST_DIR}/../engine/SurrealWidgets/include/surrealwidgets
	${CMAKE_CURRENT_LIST_DIR}/../engine/SurrealWidgets/src)
# macOS: KWNativeFontApple.cpp finds system fonts through CoreText. It skips the precompiled header because
# CoreFoundation's global Point/Rect types collide with the engine's.
if(APPLE)
	set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/KWNativeFontApple.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
	set(SURREALCOMMON_LIBS ${SURREALCOMMON_LIBS} "-framework CoreText" "-framework CoreFoundation")
endif()
