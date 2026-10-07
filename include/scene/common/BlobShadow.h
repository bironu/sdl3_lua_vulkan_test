#if !defined(COMMON_BLOBSHADOW_H_)
#define COMMON_BLOBSHADOW_H_

#include "geo/Matrix.h"
#include "vk/VulkanTexture.h"
#include <memory>

namespace SDL_
{
class VulkanWindow;
}

namespace game
{

// 地面の丸い影(ブロブ): シャドウマップを使わずに、足元に、ぼかした半透明の円を描く(軽い近似)
class BlobShadow
{
public:
	explicit BlobShadow(SDL_::VulkanWindow &window);

	// 高さfloorYの水平な床の(x, z)を中心に、半径radiusの円を描く(描画の予約)。heightは物の床からの高さで、高いほど薄く大きくなる
	void draw(SDL_::VulkanWindow &window, const geo::Matrix4x4f &viewProj, float x, float floorY, float z, float radius, float opacity,
		float height = 0.0f) const;

private:
	std::shared_ptr<VulkanTexture> texture_; // 中心が黒く、縁へなめらかに透明になる円
};

} // namespace game

#endif // COMMON_BLOBSHADOW_H_
