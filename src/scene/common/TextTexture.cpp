#include "scene/common/TextTexture.h"
#include "resources/ResourcePaths.h"
#include "resources/Resources.h"
#include "sdl/SDLTtfFont.h"
#include "sdl/SDLVulkanWindow.h"
#include <SDL3/SDL_log.h>

namespace game
{

std::shared_ptr<VulkanTexture> createTextTexture(SDL_::VulkanWindow &window, Resources &res, const std::string &text, float fontSize,
	const SDL_::Color &color)
{
	SDL_::TtfFont font(ResourcePaths::resource(res.getLuaFontName().c_str()).c_str(), fontSize);
	if(!font.is_font()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Font open error. %s", SDL_GetError());
		return nullptr;
	}
	return window.createTexture(font.renderBlendedText(text.c_str(), color));
}

} // namespace game
