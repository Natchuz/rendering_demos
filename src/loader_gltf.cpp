#include "common.h"
#include "renderer.h"

#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

void load_scene_data()
{
	ZoneScopedN("Loading scene data");

	using namespace fastgltf;

	auto start_time = std::chrono::high_resolution_clock::now();

	Parser parser;
	std::filesystem::path gltf_file = "assets/Sponza/glTF/Sponza.gltf";

	spdlog::info("Loading GLTF 2.0 file {}", gltf_file.string());

	GltfDataBuffer gltf_data;
	gltf_data.loadFromFile(gltf_file);

	auto gltf = parser.loadGLTF(&gltf_data, gltf_file.parent_path(),
	                            Options::LoadExternalBuffers | Options::LoadExternalImages);

	if (gltf->parse() != Error::None)
	throw std::runtime_error("GLTF Problem");

	auto asset = gltf->getParsedAsset();

	auto upload_writer = Mapped_Buffer_Writer(renderer->main_upload_heap_ptr);

	// Texture and sampler data loading into texture_manager.
	// This design might seem weird, but gathering all textures in one place opens doors to easier migration to
	// bindless in the future.

	struct Image_Upload {
		size_t            image_index;
		int               height, width;
		VkDeviceSize      upload_offset;
	};

	std::vector<Image_Upload>          image_uploads;
	std::unordered_map<size_t, size_t> asset_map_images; // Maps index of GLTF image to index in texture_manager

	// Upload default texture when we're at this
	{
		uint8_t pixel_data[] = { 255, 255, 255, 255 };
		upload_writer.align_next(4); // Offset need to be multiple of texel size (4)
		VkDeviceSize offset = upload_writer.write(pixel_data, 4);
		image_uploads.push_back({Texture_Manager::DEFAULT_TEXTURE, 1, 1, offset, });
	}

	for (size_t asset_image_index = 0; asset_image_index < asset->images.size(); asset_image_index++)
	{
		auto& image = asset->images[asset_image_index];

		auto data = std::get<sources::Vector>(image.data);
		
		if (data.mimeType == MimeType::None)
		throw std::runtime_error("GLTF Problem");

		int width, height, channels;
		auto pixels = stbi_load_from_memory(data.bytes.data(), static_cast<int>(data.bytes.size()),
											&width, &height, &channels, STBI_rgb_alpha);

		if (pixels == nullptr)
		throw std::runtime_error("GLTF Problem");

		Allocated_View_Image view_image; // What we will be allocating

		VkImageCreateInfo image_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType     = VK_IMAGE_TYPE_2D,
			.format        = VK_FORMAT_R8G8B8A8_SRGB,
			.extent        = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
			.mipLevels     = width == 4 ? 1u : 10u,
			.arrayLayers   = 1,
			.samples       = VK_SAMPLE_COUNT_1_BIT,
			.tiling        = VK_IMAGE_TILING_OPTIMAL,
			.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			.sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		VmaAllocationCreateInfo allocation_create_info = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };

		VmaAllocationInfo allocation_info;
		vmaCreateImage(gfx_context->vma_allocator, &image_create_info,
					   &allocation_create_info,&view_image.image,
					   &view_image.allocation, &allocation_info);
		name_object(view_image.image, "Loaded image {} {}", asset_image_index, image.name);

		// Create default image view
		create_default_image_view(gfx_context->device, image_create_info, view_image.image, nullptr, &view_image.view);
		name_object(view_image.view, "Loaded image view {} {}", asset_image_index, image.name);

		// Put in texture_manager
		texture_manager->images.push_back(view_image);
		size_t image_index = texture_manager->images.size() - 1;
		asset_map_images[asset_image_index] = image_index;

		// Push to upload heap and enqueue for upload
		upload_writer.align_next(4); // Offset need to be multiple of texel size (4)
		VkDeviceSize offset = upload_writer.write(pixels, height * width * 4);
		image_uploads.push_back({
			.image_index   = image_index,
			.height        = height,
			.width         = width,
			.upload_offset = offset,
		});

		// Free from stb_image
		stbi_image_free(pixels);
	}

	// Read and crate all samplers

	std::unordered_map<size_t, size_t> asset_map_samplers; // Maps index of GLTF image to index in texture_manager

	for (size_t asset_sampler_index = 0; asset_sampler_index < asset->samplers.size(); asset_sampler_index++) {
		auto& sampler = asset->samplers[asset_sampler_index];

		VkSamplerAddressMode address_mode_u =
		(sampler.wrapS == Wrap::ClampToEdge)    ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   :
		(sampler.wrapS == Wrap::MirroredRepeat) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
		VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap::Repeat

		VkSamplerAddressMode address_mode_v =
		(sampler.wrapT == Wrap::ClampToEdge)    ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   :
		(sampler.wrapT == Wrap::MirroredRepeat) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
		VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap::Repeat

		VkFilter mag_filter = VK_FILTER_LINEAR;
		if (sampler.magFilter.has_value() && sampler.magFilter.value() == Filter::Nearest)
		{
			mag_filter = VK_FILTER_NEAREST; // Mag filter can only have linear or nearest.
		}

		VkFilter min_filter             = VK_FILTER_LINEAR;
		VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		if (sampler.minFilter.has_value())
		{
			auto gltf_f = sampler.minFilter.value();
			min_filter = (gltf_f == Filter::NearestMipMapNearest || gltf_f == Filter::NearestMipMapLinear)
			? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
			mipmap_mode = (gltf_f == Filter::NearestMipMapNearest || gltf_f == Filter::LinearMipMapNearest)
			? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
		}

		// TODO FIX this doesn't work!!! Sampler selection in shader is broken. I'll leave anisotropy off and
		// low mip max lod for debug purposes
		VkSamplerCreateInfo sampler_create_info = {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter        = mag_filter,
			.minFilter        = min_filter,
			.mipmapMode       = mipmap_mode,
			.addressModeU     = address_mode_u,
			.addressModeV     = address_mode_v,
			.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias       = 0,
			.anisotropyEnable = false,
			.maxAnisotropy    = 16,
			.compareEnable    = false,
			.compareOp        = VK_COMPARE_OP_NEVER,
			.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
			.unnormalizedCoordinates = false,
		};
		VkSampler our_sampler;
		vkCreateSampler(gfx_context->device, &sampler_create_info, nullptr, &our_sampler);
		name_object(our_sampler, "Loaded sampler {} {}", asset_sampler_index, sampler.name);

		// Put in texture_manager
		texture_manager->samplers.push_back(our_sampler);
		size_t sampler_index = texture_manager->samplers.size() - 1;
		asset_map_samplers[asset_sampler_index] = sampler_index;
	}

	// Material parsing.

	std::unordered_map<size_t, size_t> asset_map_materials; // Maps index of GLTF image to index in material_manager

	for (size_t asset_material_index = 0; asset_material_index < asset->materials.size(); asset_material_index++) {
		auto &material = asset->materials[asset_material_index];

		if (!material.pbrData.has_value())
		throw std::runtime_error("GLTF Problem");
		PBRData& pbr_data = material.pbrData.value();

		uint32_t albedo_texture          = Texture_Manager::DEFAULT_TEXTURE;
		uint32_t albedo_sampler          = Texture_Manager::DEFAULT_SAMPLER;
		uint32_t metal_roughness_texture = Texture_Manager::DEFAULT_TEXTURE;
		uint32_t metal_roughness_sampler = Texture_Manager::DEFAULT_SAMPLER;

		// Find base color texture and sampler
		if (pbr_data.baseColorTexture.has_value())
		{
			auto& base_color_texture = pbr_data.baseColorTexture.value();

			if (base_color_texture.texCoordIndex != 0)
			throw std::runtime_error("GLTF Problem");

			auto image_index = asset->textures[base_color_texture.textureIndex].imageIndex;
			if (!image_index.has_value())
			throw std::runtime_error("GLTF Problem");
			albedo_texture = asset_map_images[image_index.value()];

			auto sampler_index = asset->textures[base_color_texture.textureIndex].samplerIndex;
			if (sampler_index.has_value())
			{
				albedo_sampler = asset_map_samplers[sampler_index.value()];
			}
		}

		// Find metalness+roughness texture and sampler
		if (pbr_data.metallicRoughnessTexture.has_value())
		{
			auto& mr_texture = pbr_data.metallicRoughnessTexture.value();

			if (mr_texture.texCoordIndex != 0)
			throw std::runtime_error("GLTF Problem");

			auto image_index = asset->textures[mr_texture.textureIndex].imageIndex;
			if (!image_index.has_value())
			throw std::runtime_error("GLTF Problem");
			metal_roughness_texture = asset_map_images[image_index.value()];

			auto sampler_index = asset->textures[mr_texture.textureIndex].samplerIndex;
			if (sampler_index.has_value())
			{
				metal_roughness_sampler = asset_map_samplers[sampler_index.value()];
			}
		}

		PBR_Material pbr_material = {
			.albedo_color            = glm::make_vec4(pbr_data.baseColorFactor.data()),
			.albedo_texture          = albedo_texture,
			.albedo_sampler          = albedo_sampler,
			.metalness_factor        = pbr_data.metallicFactor,
			.roughness_factor        = pbr_data.roughnessFactor,
			.metal_roughness_texture = metal_roughness_texture,
			.metal_roughness_sampler = metal_roughness_sampler,
		};

		// Put it in material manager
		material_manager->materials.push_back(pbr_material);
		size_t material_index = material_manager->materials.size() - 1;
		asset_map_materials[asset_material_index] = material_index;
	}

	uint32_t material_data_size = material_manager->materials.size() * sizeof(PBR_Material);
	VkDeviceSize material_data_offset = upload_writer.write(material_manager->materials.data(), material_data_size);

	// Meshes parsing and loading

	std::vector<VkBufferCopy> vertex_copies;
	std::vector<VkBufferCopy> indices_copies;

	// This maps single GLTF mesh into set of our meshes
	// (each mesh might have multiple primitives, which we consider separate meshes)
	struct Primitive
	{
		Mesh_Manager::Id mesh_id;
		uint32_t         material_id;
	};

	std::unordered_map<size_t, std::vector<Primitive>> asset_map_meshes;

	for (size_t mesh_index = 0; mesh_index < asset->meshes.size(); mesh_index++)
	{
		auto& mesh = asset->meshes[mesh_index];
		asset_map_meshes[mesh_index] = {}; // Initialize primitives list

		// Each primitive will be separate mesh
		for (auto& primitive : mesh.primitives)
		{
			// Right now, only handle triangles (conversion from other types will be implemented later)
			if (primitive.type != PrimitiveType::Triangles)
			throw std::runtime_error("GLTF Problem");

			// We don't generate indices as of now
			if (!primitive.indicesAccessor.has_value())
			throw std::runtime_error("GLTF Problem");

			// Check if all required attributes are present
			bool attributes_present =
			primitive.attributes.contains("POSITION") &&
			primitive.attributes.contains("NORMAL") &&
			primitive.attributes.contains("TEXCOORD_0");

			bool has_tangent = primitive.attributes.contains("TANGENT");

			if (!attributes_present)
			throw std::runtime_error("GLTF Problem");

			if (!has_tangent)
			{
				spdlog::info("Missing tangent!"); // Todo better logging
			}

			auto indices_accessor  = asset->accessors[primitive.indicesAccessor.value()];
			auto position_accessor = asset->accessors[primitive.attributes["POSITION"]];
			auto normal_accessor   = asset->accessors[primitive.attributes["NORMAL"]];
			auto tangent_accessor  = asset->accessors[primitive.attributes["TANGENT"]];
			auto texcoord_accessor = asset->accessors[primitive.attributes["TEXCOORD_0"]];

			// So eh, apparently, texcoord can be float, uint8_t or uint16_t... we might need to convert.
			if (texcoord_accessor.componentType != ComponentType::Float)
			throw std::runtime_error("GLTF Problem");

			// Also, indices can also be of multiple types...
			if (indices_accessor.componentType != ComponentType::UnsignedShort)
			throw std::runtime_error("GLTF Problem");

			// All attributes accessors has matching counts. This is enforced by the specs
			size_t attr_count = position_accessor.count;

			// Save offset of vertex region
			VkDeviceSize vertex_src_offset = upload_writer.offset();

			for (size_t offset = 0; offset < attr_count; offset++)
			{
				auto p = getAccessorElement<glm::vec3>(*asset, position_accessor, offset);
				auto n = getAccessorElement<glm::vec3>(*asset, normal_accessor,   offset);
				auto tx= getAccessorElement<glm::vec2>(*asset, texcoord_accessor, offset);

				auto tg = (has_tangent)
				? getAccessorElement<glm::vec4>(*asset, tangent_accessor,  offset)
				: glm::vec4(0, 0, 0, 0);

				auto attr = { p.x, p.y, p.z, n.x, n.y, n.z, tg.x, tg.y, tg.z, tg.w, tx.x, tx.y }; // Vulkan UV fix
				upload_writer.write(attr.begin(), 12 * sizeof(float));
			}

			// Save offset of indices region
			VkDeviceSize indices_src_offset = upload_writer.offset();

			// Copy indices
			copyFromAccessor<uint16_t>(*asset, indices_accessor, upload_writer.offset_ptr);
			upload_writer.advance(indices_accessor.count * sizeof(uint16_t));

			// Allocate mesh and indices
			VkResult alloc_result;
			VmaVirtualAllocationCreateInfo vertex_allocation_info = { .size = attr_count * sizeof(float) * 12 };
			VmaVirtualAllocation vertex_allocation;
			VkDeviceSize vertex_dst_offset;
			alloc_result = vmaVirtualAllocate(mesh_manager->vertex_sub_allocator,
											  &vertex_allocation_info, &vertex_allocation,
											  &vertex_dst_offset);
			if (alloc_result != VK_SUCCESS)
			throw std::runtime_error("GLTF Problem");

			VmaVirtualAllocationCreateInfo indices_allocation_info = { .size = indices_accessor.count * sizeof(uint16_t) };
			VmaVirtualAllocation indices_allocation;
			VkDeviceSize indices_dst_offset;
			alloc_result = vmaVirtualAllocate(mesh_manager->indices_sub_allocator,
											  &indices_allocation_info,&indices_allocation,
											  &indices_dst_offset);
			if (alloc_result != VK_SUCCESS)
			throw std::runtime_error("GLTF Problem");

			Mesh_Manager::Mesh_Description mesh_description = {
				.vertex_offset  = vertex_dst_offset,
				.vertex_count   = static_cast<uint32_t>(attr_count),
				.indices_offset = indices_dst_offset,
				.indices_count  = static_cast<uint32_t>(indices_accessor.count),
				.vertex_allocation  = vertex_allocation,
				.indices_allocation = indices_allocation,
			};

			Mesh_Manager::Id mesh_id = mesh_manager->next_index++;
			mesh_manager->meshes[mesh_id] = mesh_description;

			vertex_copies.push_back({
				.srcOffset = vertex_src_offset,
				.dstOffset = vertex_dst_offset,
				.size      = vertex_allocation_info.size,
			});

			indices_copies.push_back({
				.srcOffset = indices_src_offset,
				.dstOffset = indices_dst_offset,
				.size      = indices_allocation_info.size,
			});

			// Get material index
			uint32_t material_id = (primitive.materialIndex.has_value())
			? asset_map_materials[primitive.materialIndex.value()] : Material_Manager::DEFAULT_MATERIAL;

			asset_map_meshes[mesh_index].push_back({ .mesh_id = mesh_id, .material_id = material_id });
		}
	}

	// Now, let's start traversing entire scene and node hierarchy

	struct Enqueued_Node
	{
		glm::mat4 parent_transform;
		size_t    node_id;
	};
	std::deque<Enqueued_Node> nodes_queue;

	// Enqueue root nodes from scenes for traversal
	for (auto& scene : asset->scenes)
	{
		for (auto& node_id : scene.nodeIndices)
		{
			nodes_queue.push_back({
				.parent_transform = glm::translate(glm::mat4 {1.0f}, glm::vec3 {0.0f, 0.0f, 0.0f}),
				.node_id = node_id,
			});
		}
	}

	while (!nodes_queue.empty())
	{
		Enqueued_Node enqueued_node = nodes_queue.front();
		Node node = asset->nodes[enqueued_node.node_id];

		// Compose matrix if needed
		glm::mat4 transform_matrix;
		if (std::holds_alternative<Node::TransformMatrix>(node.transform))
		{
			transform_matrix = glm::make_mat4(std::get<Node::TransformMatrix>(node.transform).data());
		}
		else
		{
			Node::TRS trs = std::get<Node::TRS>(node.transform);
			auto t = glm::translate(glm::mat4 { 1.0f }, glm::make_vec3(trs.translation.data()));
			auto r = glm::mat4_cast(glm::make_quat(trs.rotation.data()));
			auto s = glm::scale(glm::mat4 { 1.0f }, glm::make_vec3(trs.scale.data()));
			transform_matrix = t * r * s;
		}

		for (auto& child_id : node.children)
		{
			nodes_queue.push_back({
				.parent_transform = transform_matrix * enqueued_node.parent_transform,
				.node_id = child_id,
			});
		}

		// Finally, spawn scene objects
		if (node.meshIndex.has_value())
		{
			for (Primitive& primitive : asset_map_meshes[node.meshIndex.value()])
			{
				Render_Object render_object = {
					.mesh_id     = primitive.mesh_id,
					.material_id = primitive.material_id,
					.transform   = transform_matrix,
				};
				scene_data->render_objects.push_back(render_object);
			}
		}

		nodes_queue.pop_front();
	}

	{
		ZoneScopedN("Upload command submission");

		flush_buffer_writer(upload_writer, gfx_context->vma_allocator, renderer->main_upload_heap.allocation);

		VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, };
		vkBeginCommandBuffer(renderer->upload_command_buffer, &begin_info);

		// Copy buffers
		vkCmdCopyBuffer(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
						mesh_manager->vertex_buffer.buffer, vertex_copies.size(), vertex_copies.data());
		vkCmdCopyBuffer(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
						mesh_manager->indices_buffer.buffer, indices_copies.size(), indices_copies.data());

		VkBufferCopy material_copy = {
			.srcOffset = material_data_offset,
			.dstOffset = 0,
			.size      = material_data_size,
		};
		vkCmdCopyBuffer(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
						material_manager->material_storage_buffer.buffer, 1, &material_copy);

		// Enqueue upload of all pending textures
		for (auto& image_upload : image_uploads)
		{
			auto vk_image = texture_manager->images[image_upload.image_index].image;

			uint32_t mip_levels = image_upload.height == 1 || image_upload.height == 4 ? 1 : 10;

			// Transition first mip to copy layout
			VkImageMemoryBarrier to_transfer_dst_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image         = vk_image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};
			vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &to_transfer_dst_barrier);

			// Enqueue copy
			VkBufferImageCopy region = {
				.bufferOffset = image_upload.upload_offset,
				.imageSubresource = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel       = 0,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
				.imageExtent = {
					.width   = static_cast<uint32_t>(image_upload.width),
					.height  = static_cast<uint32_t>(image_upload.height),
					.depth   = 1,
				},
			};
			vkCmdCopyBufferToImage(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
								   vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			// Await transfer for mip 0
			VkImageMemoryBarrier await_transfer_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.image         = vk_image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};
			vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &await_transfer_barrier);

			// Generate mipmaps
			for (uint32_t dst_mip = 1; dst_mip < mip_levels; dst_mip++)
			{
				// Move dst mip to trransfer dst
				VkImageMemoryBarrier mip_to_transfer_dst = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image         = vk_image,
					.subresourceRange = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel   = dst_mip,
						.levelCount     = 1,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
				};
				vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
									 &mip_to_transfer_dst);

				VkImageBlit2 regions = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
					.srcSubresource = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel       = dst_mip - 1,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
					.srcOffsets = {
						{ 0, 0, 0 },
						{ (image_upload.width >> dst_mip - 1), (image_upload.width >> dst_mip - 1), 1 }
					},
					.dstSubresource = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel       = dst_mip,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
					.dstOffsets = {
						{ 0, 0, 0 },
						{ (image_upload.width >> dst_mip), (image_upload.width >> dst_mip), 1 }
					},
				};

				VkBlitImageInfo2 blit_info = {
					.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
					.srcImage       = vk_image,
					.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.dstImage       = vk_image,
					.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.regionCount    = 1,
					.pRegions       = &regions,
					.filter         = VK_FILTER_LINEAR,
				};

				vkCmdBlitImage2(renderer->upload_command_buffer, &blit_info);

				// Move dst mip to trransfer src
				VkImageMemoryBarrier mip_to_transfer_src = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
					.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.image         = vk_image,
					.subresourceRange = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel   = dst_mip,
						.levelCount     = 1,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
				};
				vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
									 &mip_to_transfer_src);
			}

			// Immediately transition into proper layout
			VkImageMemoryBarrier from_transform_transition_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image         = vk_image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = mip_levels,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};

			vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &from_transform_transition_barrier);
		}

		vkEndCommandBuffer(renderer->upload_command_buffer);

		VkCommandBufferSubmitInfo command_buffer_submit_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = renderer->upload_command_buffer,
		};

		VkSubmitInfo2 submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount   = 0,
			.commandBufferInfoCount   = 1,
			.pCommandBufferInfos      = &command_buffer_submit_info,
			.signalSemaphoreInfoCount = 0,
		};
		vkQueueSubmit2(gfx_context->gfx_queue, 1, &submit_info, VK_NULL_HANDLE);

		// Block entire upload operation
		{
			ZoneScopedN("Upload command execution");
			vkDeviceWaitIdle(gfx_context->device);
		}
	}

	auto finish_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(finish_time - start_time);
	spdlog::info("Scene loaded! [{:.2f}s]", duration.count());

	// Temporary write to descriptor.
	// This is vallid only for now, as we don't have support for dynamic
	// texture streaming. Everything is loaded up-front.
	
	// Bind uniform buffer
	VkDescriptorBufferInfo global_uniform_descriptor = {
		.buffer = renderer->global_uniform_data_buffer.buffer,
		.offset = 0,
		.range  = sizeof(Global_Uniform_Data),
	};

	// Samplers
	std::vector<VkDescriptorImageInfo> sampler_descriptor_updates;
	for (uint32_t sampler_index = 0; sampler_index < texture_manager->images.size(); sampler_index++)
	{
		VkDescriptorImageInfo update_info = { .sampler = texture_manager->samplers[0] };
		sampler_descriptor_updates.push_back(update_info);
	}

	// Images
	std::vector<VkDescriptorImageInfo> image_descriptor_updates;
	for (uint32_t image_index = 0; image_index < texture_manager->images.size(); image_index++)
	{
		VkDescriptorImageInfo update_info = {
			.imageView   = texture_manager->images[image_index].view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		image_descriptor_updates.push_back(update_info);
	}

	// Bind material storage buffer
	VkDescriptorBufferInfo material_storage_descriptor = {
		.buffer = material_manager->material_storage_buffer.buffer,
		.offset = 0,
		.range  = 40000,
	};

	VkWriteDescriptorSet descriptor_set_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo     = &global_uniform_descriptor,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 1,
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(sampler_descriptor_updates.size()),
			.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
			.pImageInfo      = sampler_descriptor_updates.data(),
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 2,
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(image_descriptor_updates.size()),
			.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.pImageInfo      = image_descriptor_updates.data(),
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 3,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo     = &material_storage_descriptor,
		},
	};

	vkUpdateDescriptorSets(gfx_context->device, 4, descriptor_set_writes, 0, nullptr);
}