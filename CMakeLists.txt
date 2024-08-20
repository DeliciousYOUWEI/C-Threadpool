cmake_minimum_required(VERSION 3.8)

project(threadpool)
set(CMAKE_C_FLAGS_DEBUG -Wall)
set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/install)

add_library(threadpool SHARED ${PROJECT_SOURCE_DIR}/src/mine-threadpool.c)
target_link_libraries(threadpool pthread)
install(TARGETS threadpool LIBRARY DESTINATION lib)
install(FILES ${PROJECT_SOURCE_DIR}/src/mine-threadpool.h DESTINATION include)
