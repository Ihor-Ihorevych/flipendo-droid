# Throwaway spike: builds SDL3 + the (patched) SurrealEngine for Android arm64 into libmain.so.
cmake_minimum_required(VERSION 3.22)
project(flipendo_android LANGUAGES C CXX)

set(ENGINE ${CMAKE_CURRENT_LIST_DIR}/../src/engine)
set(ENABLE_SDL2 OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/SDL SDL3 EXCLUDE_FROM_ALL)
add_subdirectory(${ENGINE} engine EXCLUDE_FROM_ALL)

add_library(main SHARED jni/android_main.cpp)
target_include_directories(main PRIVATE ${ENGINE}/SurrealEngine)
set_target_properties(main PROPERTIES CXX_STANDARD 20)
target_link_libraries(main PRIVATE -Wl,--whole-archive SurrealCommon -Wl,--no-whole-archive SDL3::SDL3 surrealwidgets surrealgpu openmpt SurrealVideo OpenAL log android)
