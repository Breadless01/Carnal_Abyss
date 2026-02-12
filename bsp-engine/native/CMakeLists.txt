cmake_minimum_required(VERSION 3.20)
project(BSPEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_executable(Game
  src/main.cpp
)

target_include_directories(Game PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(Game PRIVATE Vulkan::Vulkan)

# Nice-to-have: warning level
if (MSVC)
  target_compile_options(Game PRIVATE /W4 /permissive-)
endif()
