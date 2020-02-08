cmake_minimum_required (VERSION 3.0)

project(TestApp)

add_subdirectory(Promise)

include_directories(${PROMISELIB_INCLUDE})

add_executable(TestApp main.cpp)
target_link_libraries(TestApp PromiseLib)
