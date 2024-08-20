cmake_minimum_required(VERSION 3.8)

project(threadpool_test)

SET(CMAKE_C_FLAGS_DEBUG -Wall)

include_directories(${PROJECT_SOURCE_DIR}/../install/include)

link_directories(${PROJECT_SOURCE_DIR}/../install/lib)

add_executable(threadpool_test ${PROJECT_SOURCE_DIR}/../src/main_test.c)
target_link_libraries(threadpool_test libthreadpool.so)
