cmake_minimum_required (VERSION 3.0)

project(TestApp)

file(GLOB APP_HEADERS "*.h")
file(GLOB APP_SRC "*.cpp")

add_executable(TestApp ${APP_SRC} ${APP_HEADERS})
target_link_libraries(TestApp)
