#include "resources/ResourcePaths.h"
#include "app/Application.h"
#include "sdl/SDLVulkanWindow.h"
#include "scene/SceneRegistry.h"
#include "scene/characterselect/CharacterSelectScene.h"
#include "scene/game/GameScene.h"
#include "scene/loading/LoadingScene.h"
#include "scene/opening/OpeningScene.h"
#include "vk/VulkanContext.h"
#include "resources/Resources.h"
#include "task/TaskManager.h"
#include "sdl/SDLMixMixer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <memory>

int main(int argc, char *argv[])
{
	Application app(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO);
	if(!app.isApplication()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL Init error. %s", SDL_GetError());
		return 1;
	}

	// メインウィンドウ=Vulkan
	auto vulkan = std::make_shared<VulkanContext>();
	if(!vulkan->initInstance()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Vulkan instance init error.");
		return 1;
	}

	Resources res;
	res.setMixer(&app.getMixer()); // 音声の読み込み(Resources::loadSound/loadMusic)に使う。appはresより先に作られ、後に壊れる
	const int width = res.getWindowWidth();
	const int height = res.getWindowHeight();

	auto mainWindow = std::make_shared<SDL_::VulkanWindow>(vulkan, "SDL3Vulkan", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, 0);
	if(!mainWindow->isReady()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Vulkan window init error.");
		return 1;
	}
	app.registerMainWindow(mainWindow);
	// Luaスクリプトの scene.change("名前") で遷移できるシーン(GameSceneはLoadingSceneが読み込みの後に作る)
	SceneRegistry::add("opening", []{ return std::make_shared<game::OpeningScene>(); });
	SceneRegistry::add("loading", []{ return std::make_shared<game::LoadingScene>(); });
	SceneRegistry::add("characterselect", []{ return std::make_shared<game::CharacterSelectScene>(); });
	SceneRegistry::add("game", []{ return std::make_shared<game::GameScene>(); });
	// 起動直後はOpeningScene(ゲーム本体の流れ)
	mainWindow->getSceneHost().registerNextScene(std::make_shared<game::OpeningScene>());

	TaskManager manager;
	const int returnCode = app.run(res, manager);
	// ウィンドウ(とそのシーン・GPUの資源が持つデータ)を、Resourcesより先に破棄する(Resourcesの終了時の点検で、手放し忘れを見つけるため)
	mainWindow.reset();
	app.releaseWindows();
	return returnCode;
}
