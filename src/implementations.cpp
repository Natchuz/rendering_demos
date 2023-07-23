//
// Implementations for all header-only libraries
//]

#include "tracy/TracyClient.cpp"

#define VOLK_IMPLEMENTATION
#include "volk.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include<tiny_obj_loader.h>

#define VMA_IMPLEMENTATION
#include<vk_mem_alloc.h>

#define SPDLOG_HEADER_ONLY
#include<spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>