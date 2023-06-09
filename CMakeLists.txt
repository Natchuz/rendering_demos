cmake_minimum_required(VERSION 3.12)
project("Rendering demos")

# ===========================
# ====== Configuration ======
# ===========================

set(CMAKE_CXX_STANDARD 20)

set(SRCS
		src/glfw_platform.cpp
		src/app.h
		src/app.cpp
		src/implementations.cpp)

set(SHADERS_SRCS
		src/shaders/triangle_frag.glsl
		src/shaders/triangle_vert.glsl
		)
set_source_files_properties(src/shaders/triangle_vert.glsl PROPERTIES ShaderType "vert" ShaderId "TRIANGLE_VERTEX")
set_source_files_properties(src/shaders/triangle_frag.glsl PROPERTIES ShaderType "frag" ShaderId "TRIANGLE_VERTEX")

# ======================
# ====== Building ======
# ======================

# --- Options & Validation ---
set(LIVEPP OFF CACHE BOOL "Enable Live++ (ON/OFF)")
set(PROFILER "NONE" CACHE STRING "Selected profiler (NONE/TRACY)")

if(NOT PROFILER MATCHES "^(NONE|TRACY)$")
	message(FATAL_ERROR "Invalid option PROFILER=${PROFILER}")
endif()

if(PROFILER MATCHES "^(TRACY)$")
	set(PROFILER_SET ON)
elseif()
	set(PROFILER_SET OFF)
endif()

if(PROFILER_SET AND LIVEPP)
	message(FATAL_ERROR "Enabling Live++ is incompatible with profiler!")
endif()

# --- Shaders ---
add_custom_target(shaders)
file(MAKE_DIRECTORY data/shaders)
foreach(FILE ${SHADERS_SRCS})
	get_filename_component(FILE_BASE_NAME ${FILE} NAME_WE)
	get_source_file_property(SHADER_TYPE ${FILE} ShaderType)
	set(OUT_NAME data/shaders/${FILE_BASE_NAME}.spv)
	add_custom_command(TARGET shaders
			COMMAND glslc -fshader-stage=${SHADER_TYPE} -o ${OUT_NAME} ${FILE}
			MAIN_DEPENDENCY ${FILE}
			COMMENT "${FILE}"
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			VERBATIM)
endforeach(FILE)

# --- Renderer itself ---
add_executable(rendering_demos ${SRCS})
target_include_directories(rendering_demos PRIVATE "src")
add_dependencies(rendering_demos shaders)

target_include_directories(rendering_demos PRIVATE "vendor")

find_package(Vulkan REQUIRED)
target_link_libraries(rendering_demos PRIVATE Vulkan::Vulkan)
set(DEFINITIONS)

# --- SDKs ---
target_include_directories(rendering_demos PRIVATE "sdk")

if(LIVEPP)
	if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/sdk/LivePP)
		if(MSVC)
			target_compile_options(rendering_demos PRIVATE /Z7 /Gm- /Gy /Gw)
		endif()

		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(rendering_demos PRIVATE
					-g -gcodeview -fms-hotpatch -ffunction-sections -Xclang -mno-constructor-aliases)
		endif()

		target_link_options(rendering_demos PRIVATE /FUNCTIONPADMIN /OPT:NOREF /OPT:NOICF /DEBUG:FULL)
		list(APPEND DEFINITIONS LIVEPP_ENABLED=1)

		message(STATUS "Live++ enabled.")
	elseif()
		message(FATAL_ERROR "Live++ enabled with LIVEPP=ON, but SDK is not configured (present)!")
	endif()
endif()

if(PROFILER_SET)
	list(APPEND DEFINITIONS TRACY_ENABLE)
endif()

target_compile_definitions(rendering_demos PRIVATE ${DEFINITIONS})

# --- ImGui ---
set(IMGUI_SRCS
		vendor/imgui/imgui.cpp
		vendor/imgui/imgui_demo.cpp
		vendor/imgui/imgui_draw.cpp
		vendor/imgui/imgui_tables.cpp
		vendor/imgui/imgui_widgets.cpp
		vendor/imgui/backends/imgui_impl_glfw.cpp
		vendor/imgui/backends/imgui_impl_vulkan.cpp
		)
add_library(imgui ${IMGUI_SRCS})
target_include_directories(imgui PRIVATE "vendor/imgui")
target_link_libraries(imgui PRIVATE Vulkan::Vulkan)
target_link_libraries(imgui PRIVATE glfw)

target_include_directories(rendering_demos PRIVATE "vendor/imgui")
target_link_libraries(rendering_demos PRIVATE imgui)

# --- Vcpkg ---
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(rendering_demos PRIVATE glfw)

find_package(glm CONFIG REQUIRED)
target_link_libraries(rendering_demos PRIVATE glm::glm)

find_package(tinyobjloader CONFIG REQUIRED)
target_link_libraries(rendering_demos PRIVATE tinyobjloader::tinyobjloader)

find_package(unofficial-vulkan-memory-allocator CONFIG REQUIRED)
target_link_libraries(rendering_demos PRIVATE unofficial::vulkan-memory-allocator::vulkan-memory-allocator)