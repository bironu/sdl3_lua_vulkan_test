#include "scene/common/BlobShadow.h"
#include "geo/AffineMap.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace game
{

namespace
{
const geo::Quaternionf kRotate90X = geo::Quaternionf::createRotater(-std::numbers::pi_v<float> / 2, geo::Vector3f(1.0f, 0.0f, 0.0f));
}

BlobShadow::BlobShadow(SDL_::VulkanWindow &window)
{
	constexpr int kSize = 64;
	auto image = std::make_shared<SDL_::Image>(kSize, kSize);
	for(int y = 0; y < kSize; ++y){
		for(int x = 0; x < kSize; ++x){
			const float dx = (static_cast<float>(x) + 0.5f) / kSize * 2.0f - 1.0f;
			const float dy = (static_cast<float>(y) + 0.5f) / kSize * 2.0f - 1.0f;
			const float t = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
			const float alpha = t * t * (3.0f - 2.0f * t); // 中心から縁へ、なめらかに0へ
			SDL_WriteSurfacePixel(image->get(), x, y, 0, 0, 0, static_cast<Uint8>(alpha * 255.0f + 0.5f));
		}
	}
	texture_ = window.createTexture(image);
}

void BlobShadow::draw(SDL_::VulkanWindow &window, const geo::Matrix4x4f &viewProj, float x, float floorY, float z, float radius,
	float opacity, float height) const
{
	if(!texture_){
		return;
	}
	const float spread = 1.0f + height * 0.5f;
	geo::AffineMap model;
	model.setPos(geo::Vector3f(x, floorY + 0.01f, z)); // 床から少し浮かせて、床と同じ深度でちらつかないようにする
	model.setRotation(kRotate90X); // 板(+Z向き)を水平(+Y向き)に寝かせる
	model.setScale(geo::Vector3f(radius * 2.0f * spread, radius * 2.0f * spread, 1.0f));
	window.drawSprite3D(texture_, viewProj * model.getMatrix(), 1.0f, 1.0f, 1.0f, opacity / spread);
}

} // namespace game
