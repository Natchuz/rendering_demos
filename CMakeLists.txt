cmake_minimum_required(VERSION 3.12)
project("Rendering demos")

find_package(Vulkan REQUIRED)

set(CMAKE_CXX_STANDARD 20)

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
# set_property(TARGET rendering_demos PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:rendering_demos>)
# target_include_directories(rendering_demos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(rendering_demos src/main.cpp)
target_link_libraries(rendering_demos PRIVATE Vulkan::Vulkan)

# Vcpkg
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(rendering_demos PRIVATE glfw)