cmake_minimum_required(VERSION 3.20)
project(BSPEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development)

add_executable(Game
  src/main.cpp
  src/scripting/PythonHost.cpp
  src/scripting/EngineModule.cpp
  src/scripting/EngineModule.h
  src/scripting/PythonHost.h
)

target_include_directories(Game PRIVATE ${Vulkan_INCLUDE_DIRS})
target_include_directories(Game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Game PRIVATE Vulkan::Vulkan)

# Embedded Python (gameplay scripts)
target_link_libraries(Game PRIVATE Python3::Python)

# Nice-to-have: warning level
if (MSVC)
  target_compile_options(Game PRIVATE /W4 /permissive-)
endif()
