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

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
  GIT_SHALLOW 1
)

FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket
  GIT_TAG v11.4.6
  GIT_SHALLOW 1
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
  GIT_SHALLOW 1
)

FetchContent_MakeAvailable(raylib)
FetchContent_MakeAvailable(spdlog)
FetchContent_MakeAvailable(ixwebsocket)
FetchContent_MakeAvailable(nlohmann_json)

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
  ixwebsocket
  nlohmann_json::nlohmann_json
)
