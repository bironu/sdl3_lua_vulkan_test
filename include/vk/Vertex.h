#if !defined(VK_VERTEX_H_)
#define VK_VERTEX_H_

#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace vk_
{

// 頂点バッファ1要素。res/shaders/triangle.vertのlocation 0,1,2,3に対応する
struct Vertex
{
	float position[3];
	float color[3];
	float uv[2];
	float normal[3]; // ワールド変換前(モデル空間)の法線。単位ベクトル

	static VkVertexInputBindingDescription bindingDescription()
	{
		return {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	}

	static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions()
	{
		return {{
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
			{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
			{3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
		}};
	}
};

// スキニング(ボーンで頂点を動かす)用の頂点。Vertexにボーン番号4つと重み4つ、頂点番号を足したもの。
// 位置・法線はモデルの元の座標系(PMXは左手系)のまま持ち、頂点シェーダーがスキニングしてから右手系に直す
// (res/shaders/triangle_skin.vert、shadow_skin.vert)。未使用の枠はボーン-1・重み0
struct SkinnedVertex
{
	Vertex base;
	int32_t bones[4];
	float weights[4];
	int32_t source; // 元のモデルでの頂点番号(頂点モーフの移動量の参照用)。モーフを使わないモデルは-1

	static VkVertexInputBindingDescription bindingDescription()
	{
		return {0, sizeof(SkinnedVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	}

	static std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions()
	{
		return {{
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, base) + offsetof(Vertex, position)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, base) + offsetof(Vertex, color)},
			{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SkinnedVertex, base) + offsetof(Vertex, uv)},
			{3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, base) + offsetof(Vertex, normal)},
			{4, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(SkinnedVertex, bones)},
			{5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SkinnedVertex, weights)},
			{6, 0, VK_FORMAT_R32_SINT, offsetof(SkinnedVertex, source)},
		}};
	}
};

} // namespace vk_

#endif // VK_VERTEX_H_
