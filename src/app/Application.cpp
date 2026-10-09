#include "app/Application.h"
#include "sdl/SDLWindow.h"
#include "sdl/SDLMixMixer.h"
#include "resources/Resources.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
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
	keybordState_ = is_application_ ? ::SDL_GetKeyboardState(nullptr) : nullptr;
	if (is_mixer_) {
		mixer_->allocateChannels(8);
	}
}

Application::~Application()
{
	keybordState_ = nullptr;
	releaseWindows(); // SDL_Quit()より前にウィンドウ(Vulkan資源含む)を破棄する
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
	unregisterMainWindow();
	listWindow_.clear();
}

void Application::registerMainWindow(std::shared_ptr<SDL_::Window> mainWindow)
{
	if (!mainWindow) {
		return;
	}
	mainWindow_ = mainWindow;
	registerWindow(std::move(mainWindow));
}

void Application::unregisterMainWindow()
{
	if (mainWindow_) {
		std::erase(listWindow_, mainWindow_);
		mainWindow_.reset();
	}
}

void Application::registerWindow(std::shared_ptr<SDL_::Window> window)
{
	if (!window || std::find(listWindow_.begin(), listWindow_.end(), window) != listWindow_.end()) {
		return;
	}
	listWindow_.push_back(std::move(window));
}

void Application::unregisterWindow(std::shared_ptr<SDL_::Window> window)
{
	if (window && window == mainWindow_) {
		unregisterMainWindow();
		return;
	}
	std::erase(listWindow_, window);
}

std::shared_ptr<SDL_::Window> Application::getWindow(SDL_WindowID id) const
{
	for (const auto &window : listWindow_) {
		if (window->getWindowId() == id) {
			return window;
		}
	}
	return nullptr;
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

#ifndef NDEBUG
// ウィンドウイベントのデバッグログ。Release(NDEBUG)ビルドでは出さない。
// SDL3ではSDL_WINDOWEVENT+ネストしたevent.window.eventによる分岐は廃止され、
// 個々のウィンドウイベントがトップレベルのevent.typeとして独立している
void logWindowEvent(const SDL_Event &event)
{
	const char *name = nullptr;
	switch (event.type) {
	case SDL_EVENT_WINDOW_SHOWN: name = "shown"; break;
	case SDL_EVENT_WINDOW_HIDDEN: name = "hidden"; break;
	case SDL_EVENT_WINDOW_EXPOSED: name = "exposed"; break;
	case SDL_EVENT_WINDOW_MOVED: name = "moved"; break;
	case SDL_EVENT_WINDOW_RESIZED: name = "resized"; break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: name = "pixel size changed"; break;
	case SDL_EVENT_WINDOW_MINIMIZED: name = "minimized"; break;
	case SDL_EVENT_WINDOW_MAXIMIZED: name = "maximized"; break;
	case SDL_EVENT_WINDOW_RESTORED: name = "restored"; break;
	case SDL_EVENT_WINDOW_MOUSE_ENTER: name = "mouse enter"; break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE: name = "mouse leave"; break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED: name = "focus gained"; break;
	case SDL_EVENT_WINDOW_FOCUS_LOST: name = "focus lost"; break;
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED: name = "close requested"; break;
	case SDL_EVENT_WINDOW_HIT_TEST: name = "hit test"; break;
	default: return;
	}
	SDL_Log("Window %u %s (%d,%d)", event.window.windowID, name, event.window.data1, event.window.data2);
}
#else
void logWindowEvent(const SDL_Event &) {}
#endif

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
				const SDL_WindowID id = getEventWindowId(event);
				const auto target = id != 0 ? getWindow(id) : mainWindow_;
				// 既に閉じたウィンドウ宛てのイベントは破棄する
				if (target) {
					target->getSceneHost().dispatch(event);
				}
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

	logWindowEvent(event);
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

	case SDL_EVENT_WINDOW_RESIZED:
		res.setWindowWidth(event.window.data1);
		res.setWindowHeight(event.window.data2);
		break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		// Pixel size が変更されたので、WindowのBackbufferを再生成する
		if (auto window = getWindow(event.window.windowID)) {
			window->onPixelSizeChanged();
			window->requestUpdate();
		}
		break;

	default:
		break;
	}

	return result;
}
