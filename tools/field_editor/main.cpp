// フィールドエディタ: 地面のタイルを格子の上に並べて、res/field/*.lua へ保存する(ゲーム本体とは別の実行ファイル。engineライブラリを使う)

#include "FieldEditorScene.h"
#include "app/Application.h"
#include "resources/Resources.h"
#include "sdl/SDLMixMixer.h"
#include "sdl/SDLVulkanWindow.h"
#include "task/TaskManager.h"
#include "vk/VulkanContext.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <memory>

int main(int, char *[])
{
	Application app(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO);
	if(!app.isApplication()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL Init error. %s", SDL_GetError());
		return 1;
	}
	auto vulkan = std::make_shared<VulkanContext>();
	if(!vulkan->initInstance()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Vulkan instance init error.");
		return 1;
	}
	Resources res;
	res.setMixer(&app.getMixer());
	auto window = std::make_shared<SDL_::VulkanWindow>(vulkan, "Field Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		res.getWindowWidth(), res.getWindowHeight(), 0);
	if(!window->isReady()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Vulkan window init error.");
		return 1;
	}
	app.registerMainWindow(window);
	window->getSceneHost().registerNextScene(std::make_shared<FieldEditorScene>());

	TaskManager manager;
	const int returnCode = app.run(res, manager);
	window.reset();
	app.releaseWindows();
	return returnCode;
}
