#if !defined(COMMON_TEXTTEXTURE_H_)
#define COMMON_TEXTTEXTURE_H_

#include "sdl/SDLColor.h"
#include "vk/VulkanTexture.h"
#include <memory>
#include <string>

namespace SDL_
{
class VulkanWindow;
}
class Resources;

namespace game
{

// 文字列をフォント(Resourcesのlua設定のフォント)で描いて、スプライト用のテクスチャにする。失敗したらnullptr
std::shared_ptr<VulkanTexture> createTextTexture(SDL_::VulkanWindow &window, Resources &res, const std::string &text, float fontSize,
	const SDL_::Color &color);

} // namespace game

#endif // COMMON_TEXTTEXTURE_H_
