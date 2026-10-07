#include "vk/VulkanMesh.h"
#include <algorithm>

VulkanMesh::VulkanMesh(std::shared_ptr<VulkanContext> ctx, const vk_::Vertex* vertices, uint32_t vertexCount,
	const uint32_t* indices, uint32_t indexCount)
	: VulkanMesh(std::move(ctx), vertices, sizeof(vk_::Vertex), vertexCount, indices, indexCount, false)
{
}

VulkanMesh::VulkanMesh(std::shared_ptr<VulkanContext> ctx, const void* vertices, uint32_t vertexStride, uint32_t vertexCount,
	const uint32_t* indices, uint32_t indexCount, bool skinned)
	: vertexBuffer_(ctx, static_cast<VkDeviceSize>(vertexStride) * vertexCount,
		skinned ? (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) // スキニングのメッシュは、コンピュートが入力として読む
	, indexBuffer_(ctx, sizeof(uint32_t) * indexCount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
	, indexCount_(indexCount)
	, vertexCount_(vertexCount)
	, skinned_(skinned)
{
	if(isValid()){
		vertexBuffer_.write(vertices, static_cast<VkDeviceSize>(vertexStride) * vertexCount);
		indexBuffer_.write(indices, sizeof(uint32_t) * indexCount);
	}
	if(vertexCount > 0){
		// 位置(float3)は、どの頂点型でも要素の先頭にある
		const auto* bytes = static_cast<const uint8_t*>(vertices);
		for(int k = 0; k < 3; ++k){
			boundsMin_[k] = boundsMax_[k] = reinterpret_cast<const float*>(bytes)[k];
		}
		for(uint32_t i = 1; i < vertexCount; ++i){
			const float* p = reinterpret_cast<const float*>(bytes + static_cast<size_t>(vertexStride) * i);
			for(int k = 0; k < 3; ++k){
				boundsMin_[k] = std::min(boundsMin_[k], p[k]);
				boundsMax_[k] = std::max(boundsMax_[k], p[k]);
			}
		}
	}
}
