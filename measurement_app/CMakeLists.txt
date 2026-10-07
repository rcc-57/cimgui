cmake_minimum_required(VERSION 3.15)
project(measurement_control LANGUAGES C CXX)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Based on backend_test/example_glfw_opengl3. Prefer an installed GLFW;
# otherwise build the same pinned GLFW version as the example.
find_package(glfw3 3.3 QUIET)
if(NOT TARGET glfw)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01)
    FetchContent_MakeAvailable(glfw)
endif()

set(CIMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT EXISTS "${CIMGUI_DIR}/imgui/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui missing: run git submodule update --init --recursive")
endif()
find_package(OpenGL REQUIRED)
add_library(measurement_gui_dependencies STATIC
    ${CIMGUI_DIR}/cimgui.cpp
    ${CIMGUI_DIR}/imgui/imgui.cpp
    ${CIMGUI_DIR}/imgui/imgui_draw.cpp
    ${CIMGUI_DIR}/imgui/imgui_demo.cpp
    ${CIMGUI_DIR}/imgui/imgui_tables.cpp
    ${CIMGUI_DIR}/imgui/imgui_widgets.cpp
    ${CIMGUI_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CIMGUI_DIR}/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(measurement_gui_dependencies PUBLIC ${CIMGUI_DIR} ${CIMGUI_DIR}/imgui)
target_compile_definitions(measurement_gui_dependencies PRIVATE
    IMGUI_USER_CONFIG="../cimconfig.h" IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1
    "IMGUI_IMPL_API=extern \"C\"")
target_link_libraries(measurement_gui_dependencies PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})

add_executable(measurement_control main.c instrument_ui.c)
target_compile_definitions(measurement_control PRIVATE CIMGUI_USE_GLFW CIMGUI_USE_OPENGL3)
target_link_libraries(measurement_control PRIVATE measurement_gui_dependencies)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(measurement_control PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(APPLE)
    target_compile_definitions(measurement_control PRIVATE GL_SILENCE_DEPRECATION)
endif()
