cmake_minimum_required(VERSION 3.28)
project(wrpl)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(EMSCRIPTEN)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(vendor/zlib)

if(EMSCRIPTEN)
  if(NOT TARGET zlib)
    add_library(zlib ALIAS zlibstatic)
  endif()
endif()

add_subdirectory(vendor/dagutils)

add_library(wrpl_lib STATIC)
target_sources(wrpl_lib PUBLIC FILE_SET CXX_MODULES FILES
  modules/parser.cpp
  modules/deserializer.cpp
)
target_link_libraries(wrpl_lib PUBLIC
  zlib
  dagutils
)

if(EMSCRIPTEN)
  target_compile_options(wrpl_lib PUBLIC
      -fexceptions
      -D__EMSCRIPTEN__
  )
endif()

add_executable(${PROJECT_NAME}
  src/main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE wrpl_lib)
if(EMSCRIPTEN)
  add_executable(wrpl_wasm modules/bindings.cpp)
  target_link_libraries(wrpl_wasm PRIVATE wrpl_lib)
  
  target_compile_options(wrpl_wasm PRIVATE
      -fexceptions
      -D__EMSCRIPTEN__
  )

  target_link_options(wrpl_wasm PRIVATE
      "SHELL:-s WASM=1"
      "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"
      "SHELL:-s ALLOW_MEMORY_GROWTH=1"
      "SHELL:-s NO_EXIT_RUNTIME=0"
      "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
      "SHELL:--bind"
  )
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
