#if !defined(VULKANMESH_H_)
#define VULKANMESH_H_

#include "misc/Uncopyable.h"
#include "vk/Vertex.h"
#include "vk/VulkanBuffer.h"
#include "vk/VulkanContext.h"
#include <cstdint>
#include <memory>

// 頂点バッファ+インデックスバッファ(uint32)の組。複数の描画オブジェクトで共有できる
// (VulkanWindow::draw()にshared_ptrで渡す。GPU使用中に解放されないようウィンドウが保持する)
class VulkanMesh
{
public:
	UNCOPYABLE(VulkanMesh);
	VulkanMesh(std::shared_ptr<VulkanContext> ctx, const vk_::Vertex* vertices, uint32_t vertexCount,
		const uint32_t* indices, uint32_t indexCount);
	// 頂点の型を問わない版。skinned=trueならvk_::SkinnedVertex(スキニング用のパイプラインで描く)
	VulkanMesh(std::shared_ptr<VulkanContext> ctx, const void* vertices, uint32_t vertexStride, uint32_t vertexCount,
		const uint32_t* indices, uint32_t indexCount, bool skinned);
	bool isSkinned() const { return skinned_; }

	bool isValid() const { return vertexBuffer_.isValid() && indexBuffer_.isValid(); }
	VkBuffer vertexBuffer() const { return vertexBuffer_.get(); }
	VkBuffer indexBuffer() const { return indexBuffer_.get(); }
	uint32_t indexCount() const { return indexCount_; }
	uint32_t vertexCount() const { return vertexCount_; }
	// 頂点バッファの位置から求めた、メッシュ空間のAABB(フラスタムカリング用。スキニングのメッシュは休止ポーズの範囲で、元の座標系のまま)
	const float* boundsMin() const { return boundsMin_; }
	const float* boundsMax() const { return boundsMax_; }

private:
	VulkanBuffer vertexBuffer_;
	VulkanBuffer indexBuffer_;
	uint32_t indexCount_;
	uint32_t vertexCount_;
	bool skinned_;
	float boundsMin_[3] = {0.0f, 0.0f, 0.0f};
	float boundsMax_[3] = {0.0f, 0.0f, 0.0f};
};

#endif // VULKANMESH_H_
