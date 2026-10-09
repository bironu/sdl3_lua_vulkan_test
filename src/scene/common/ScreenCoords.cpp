#include "scene/common/ScreenCoords.h"
#include "sdl/SDLWindow.h"
#include "ui/UiContext.h"
#include <algorithm>

namespace game
{

std::pair<float, float> windowToScreen(const SDL_::Window &window, const ui::UiContext &ctx, float wx, float wy)
{
	const auto size = window.getSize();
	return {wx / static_cast<float>(std::max(size.getX(), 1)) * ctx.screenWidth(),
		wy / static_cast<float>(std::max(size.getY(), 1)) * ctx.screenHeight()};
}

} // namespace game
