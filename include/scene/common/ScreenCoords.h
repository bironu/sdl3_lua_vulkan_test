#if !defined(COMMON_SCREENCOORDS_H_)
#define COMMON_SCREENCOORDS_H_

#include <utility>

namespace SDL_
{
class Window;
}
namespace ui
{
class UiContext;
}

namespace game
{

// ウィンドウの座標(ピクセル)を、UIの論理画面の座標へ変える
std::pair<float, float> windowToScreen(const SDL_::Window &window, const ui::UiContext &ctx, float wx, float wy);

} // namespace game

#endif // COMMON_SCREENCOORDS_H_
