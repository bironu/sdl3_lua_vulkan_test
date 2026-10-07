#include "app/Application.h"
#include "sdl/SDLWindow.h"
#include "sdl/SDLMixMixer.h"
#include "resources/Resources.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3/SDL_log.h>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <vector>

const bool *Application::keybordState_ = nullptr;

Application::Application(Uint32 flags)
	: is_application_(::SDL_Init(flags))
	, is_ttf_(::TTF_Init())
	, is_mixer_(::MIX_Init())
	, mixer_(std::make_unique<SDL_::Mix_::Mixer>())
	, listWindow_()
	, mainWindow_()
	, return_code_(0)
{
    keybordState_ = ::SDL_GetKeyboardState(nullptr);
	mixer_->allocateChannels(8);
}

Application::~Application()
{
    keybordState_ = nullptr;
	listWindow_.clear();
	mainWindow_.reset(); // SDL_Quit()より前にウィンドウ(Vulkan資源含む)を破棄する
	mixer_.reset();
    if (isMixer()){
        ::MIX_Quit();
    }
	if (isTtf()){
		::TTF_Quit();
	}
	if (isApplication()){
		::SDL_Quit();
	}
}

void Application::releaseWindows()
{
	listWindow_.clear();
	mainWindow_.reset();
}

void Application::registerMainWindow(std::shared_ptr<SDL_::Window> mainWindow)
{
	mainWindow_ = mainWindow;
	registerWindow(mainWindow);
}

std::shared_ptr<SDL_::Window> Application::getMainWindow()
{
	return mainWindow_;
}

void Application::registerWindow(std::shared_ptr<SDL_::Window> window)
{
	listWindow_.push_back(window);
}

void Application::unregisterWindow(std::shared_ptr<SDL_::Window> window)
{
	listWindow_.erase(std::remove(std::begin(listWindow_), std::end(listWindow_), window), std::end(listWindow_));
}

std::shared_ptr<SDL_::Window> Application::getWindow(int id)
{
	std::shared_ptr<SDL_::Window> result;
	for(auto window : listWindow_) {
		if(window->getWindowId() == id) {
			result = window;
			break;
		}
	}
	return result;
}

namespace
{

// イベントの宛先ウィンドウID。ウィンドウに紐づかないイベントは0を返す
SDL_WindowID getEventWindowId(const SDL_Event &event)
{
	if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
		return event.window.windowID;
	}
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return event.key.windowID;
	case SDL_EVENT_TEXT_INPUT:
		return event.text.windowID;
	case SDL_EVENT_MOUSE_MOTION:
		return event.motion.windowID;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return event.button.windowID;
	case SDL_EVENT_MOUSE_WHEEL:
		return event.wheel.windowID;
	default:
		return 0;
	}
}

} // namespace

int Application::run(Resources &res, TaskManager &manager)
{
	// 各ウィンドウに事前登録されたSceneを開始する
	for (auto &window : listWindow_) {
		window->getSceneHost().attach(this, &res, &manager);
	}
	const auto startTick = getTickCount();
	for (auto &window : listWindow_) {
		window->getSceneHost().updateScenes(startTick);
	}

	SDL_Event event;
	bool idle(false);
	// メインウィンドウのSceneが無くなったらアプリ終了
	while(mainWindow_ && mainWindow_->getSceneHost().hasScene()) {
		const auto tick = getTickCount();
		if(::SDL_PollEvent(&event)){
			if (!handlePreEvent(res, manager, event)) {
				// 宛先ウィンドウのSceneへ配送。ウィンドウに紐づかないイベント
				// (ジョイスティック等)はメインウィンドウへ送る
				auto target = getWindow(getEventWindowId(event));
				(target ? target : mainWindow_)->getSceneHost().dispatch(event);
			}
			idle = true;
		}
		else if (idle) {
			// 全ウィンドウのonIdle()を回す(短絡評価しない)。どれかが継続中ならidle継続
			bool stillRunning = false;
			for (auto &window : std::vector(listWindow_)) {
				stillRunning |= window->getSceneHost().onIdle(tick);
			}
			idle = stillRunning;
		}
		else {
			if (!::SDL_WaitEvent(nullptr)){
				::SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_WaitEvent Error!! %s", ::SDL_GetError());
				quit(1);
			}
		}

		// Scene遷移を各ウィンドウで解決する(詳細はSceneHost::updateScenes)。
		// Sceneが無くなったサブウィンドウは閉じる(一覧から外す)
		for (auto &window : std::vector(listWindow_)) {
			window->getSceneHost().updateScenes(tick);
			if (window != mainWindow_ && !window->getSceneHost().hasScene()) {
				unregisterWindow(window);
			}
		}
	}
	for (auto &window : listWindow_) {
		window->getSceneHost().clearResumeStack();
	}
	return return_code_;
}

void Application::quit(const int val)
{
	SDL_Event event = {SDL_EVENT_QUIT};
	return_code_ = val;
	::SDL_PushEvent(&event);
}

bool Application::handlePreEvent(Resources &res, TaskManager &manager, SDL_Event &event)
{
	bool result = false;

	switch(event.type)
	{
	case SDL_EVENT_QUIT:
		for (auto &window : listWindow_) {
			window->getSceneHost().terminate();
		}
		result = true;
		break;

	case SDL_EVENT_GAMEPAD_ADDED:
		res.addGamepad(event.gdevice.which);
		break;

	case SDL_EVENT_GAMEPAD_REMOVED:
		res.removeGamepad(event.gdevice.which);
		break;

	case SDL_EVENT_JOYSTICK_ADDED:
		res.addJoyDevice(event.jdevice);
		break;

	case SDL_EVENT_JOYSTICK_REMOVED:
		res.removeJoyDevice(event.jdevice);
		break;

	// SDL3ではSDL_WINDOWEVENT+ネストしたevent.window.eventによる分岐は廃止され、
	// 個々のウィンドウイベントがトップレベルのevent.typeとして独立している
	case SDL_EVENT_WINDOW_SHOWN:
		SDL_Log("Window %d shown", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_HIDDEN:
		SDL_Log("Window %d hidden", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_EXPOSED:
		SDL_Log("Window %d exposed", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_MOVED:
		SDL_Log("Window %d moved to %d,%d",
				event.window.windowID, event.window.data1, event.window.data2);
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		res.setWindowWidth(event.window.data1);
		res.setWindowHeight(event.window.data2);
		SDL_Log("Window %d resized to %dx%d",
				event.window.windowID, event.window.data1, event.window.data2);
		break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        // Pixel size が変更されたので、WindowのBackbufferを再生成する
        if (auto window = getWindow(event.window.windowID)) {
            window->onPixelSizeChanged();
            window->requestUpdate();
        }
		SDL_Log("Window %d pixel size changed to %dx%d",
				event.window.windowID, event.window.data1, event.window.data2);
		break;
	case SDL_EVENT_WINDOW_MINIMIZED:
		SDL_Log("Window %d minimized", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_MAXIMIZED:
		SDL_Log("Window %d maximized", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_RESTORED:
		SDL_Log("Window %d restored", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
		SDL_Log("Mouse entered window %d", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		SDL_Log("Mouse left window %d", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		SDL_Log("Window %d gained keyboard focus", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		SDL_Log("Window %d lost keyboard focus", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		SDL_Log("Window %d closed", event.window.windowID);
		break;
	case SDL_EVENT_WINDOW_HIT_TEST:
		SDL_Log("Window %d has a special hit test", event.window.windowID);
		break;

	default:
		break;
	}

	return result;
}

void Application::setTimer(int interval, SDL_::Timer::Callback timer_proc)
{
    timer_.reset();
    timer_ = std::make_unique<SDL_::Timer>(interval, timer_proc);
}

void Application::killTimer(void)
{
    timer_.reset();
}

//void Application::waitFrame()
//{
//	static const Uint32 wait((1000<<16)/60);
//	static Uint32 lasttime(0); // last time
//	static Uint32 passage(0);
//
//	const Uint32 t(::SDL_GetTicks()); // now time
//	const Uint32 progress(t - lasttime);
//	passage = (passage & 0xffff) + wait;
//	const Uint32 twait(passage >> 16);
//	if(progress >= twait){
//		lasttime = t;
//	}
//	else{
//		::SDL_Delay(twait-progress);
//		while((::SDL_GetTicks()-lasttime) < twait);
//		lasttime += twait;
//	}
//}
