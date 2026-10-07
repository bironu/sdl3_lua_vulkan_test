#if !defined(VK_PRIMITIVEMESHES_H_)
#define VK_PRIMITIVEMESHES_H_

#include "vk/VulkanContext.h"
#include "vk/VulkanMesh.h"
#include <memory>

// 床・立方体など、コードで作る簡単な形のメッシュ(ライティング付きの頂点)
namespace vk_
{

// 水平な正方形の床(1x1、y=0、法線+Y)。UVは0〜repeat
std::shared_ptr<VulkanMesh> createFloorMesh(const std::shared_ptr<VulkanContext> &ctx, float repeat);

// 一辺1の立方体(面ごとに頂点を持つ24頂点。UVは面全体に0〜1)。
// lit=falseなら法線を0にした「自発光」メッシュ(シェーダーが光源計算をしない。点光源の目印用)。
// faceColors=trueなら面ごとに色が違う(確認用)。falseなら頂点色は白(テクスチャの色のまま。壁・箱用)
std::shared_ptr<VulkanMesh> createCubeMesh(const std::shared_ptr<VulkanContext> &ctx, bool lit = true, bool faceColors = true);

} // namespace vk_

#endif // VK_PRIMITIVEMESHES_H_
