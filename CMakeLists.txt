cmake_minimum_required(VERSION 3.10..3.31)

project(srclient
  VERSION 0.0.1
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# packages -------------------------------------------------------------------#

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) # don't build the examples
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE) # don't build the example games
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW 1
)
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
)
FetchContent_MakeAvailable(spdlog)

# targets --------------------------------------------------------------------#

# add subdirs here...

add_executable(${PROJECT_NAME}
  src/main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  raylib
  spdlog
)
