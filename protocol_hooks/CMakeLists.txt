cmake_minimum_required(VERSION 3.13.0)
project(protocol_hooks)

set(CMAKE_CXX_STANDARD 14)

set(DEPS
    cares_service
    common_utils
    ${COMMON_DEPS}
   )

set(SOURCES
    src/basic_protocol.cc
   )

add_library(${PROJECT_NAME} OBJECT ${SOURCES})
target_link_libraries(${PROJECT_NAME} PUBLIC ${DEPS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

