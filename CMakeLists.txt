cmake_minimum_required(VERSION 3.28)
project(wrpl)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_subdirectory(vendor/zlib)
add_subdirectory(vendor/dagutils)

add_executable(${PROJECT_NAME}
  src/main.cpp
)

target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES
  modules/parser.cpp
  modules/deserializer.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  zlib
  dagutils
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
