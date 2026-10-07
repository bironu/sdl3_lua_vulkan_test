#include "scene/SampleScene.h"
#include "sdl/SDLRendererWindow.h"
#include "sdl/SDLRenderer.h"
#include <SDL3/SDL_events.h>

void SampleScene::dispatch(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
		quit();
	}
}

void SampleScene::onSuspend()
{
}

bool SampleScene::onIdle(uint32_t tick)
{
	const bool stillRunning = Scene::onIdle(tick);
	// SDL_Rendererを持つウィンドウ専用のSceneなので、ウィンドウはSDL_::RendererWindow
	static_cast<SDL_::RendererWindow&>(getWindow()).getRenderer().setDrawColor(SDL_::Color(0, 0, 0, 255));
	swap();
	return stillRunning;
}
