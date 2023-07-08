cmake_minimum_required(VERSION 3.12)
project("Rendering demos")

# ===========================
# ====== Configuration ======
# ===========================

set(CMAKE_CXX_STANDARD 20)

set(SRCS
		src/main.cpp
		src/app.h
		src/app.cpp
		)

set(SHADERS_SRCS
		src/shaders/triangle_frag.glsl
		src/shaders/triangle_vert.glsl
		)
set_source_files_properties(src/shaders/triangle_vert.glsl PROPERTIES ShaderType "vert" ShaderId "TRIANGLE_VERTEX")
set_source_files_properties(src/shaders/triangle_frag.glsl PROPERTIES ShaderType "frag" ShaderId "TRIANGLE_VERTEX")

# ======================
# ====== Building ======
# ======================

# Compile shaders
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

# Renderer itself
add_executable(rendering_demos ${SRCS})
target_include_directories(rendering_demos PRIVATE "src")
add_dependencies(rendering_demos shaders)

find_package(Vulkan REQUIRED)
target_link_libraries(rendering_demos PRIVATE Vulkan::Vulkan)

# --- Vcpkg ---
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(rendering_demos PRIVATE glfw)