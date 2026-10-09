#if !defined(COMMON_SCENEWINDOW_H_)
#define COMMON_SCENEWINDOW_H_

#include "scene/Scene.h"
#include "sdl/SDLVulkanWindow.h"

namespace game
{

// Sceneが属するウィンドウを、VulkanWindowとして取り出す(Vulkan描画のSceneだけが使う)
inline SDL_::VulkanWindow &vulkanWindow(Scene &scene)
{
	return static_cast<SDL_::VulkanWindow &>(scene.getWindow());
}

} // namespace game

#endif // COMMON_SCENEWINDOW_H_
