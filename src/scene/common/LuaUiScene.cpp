#include "scene/common/LuaUiScene.h"
#include "app/Application.h"
#include "scene/SceneHost.h"
#include "scene/common/SceneWindow.h"
#include "scene/common/ScreenCoords.h"
#include "scene/SceneRegistry.h"
#include "sdl/SDLVulkanWindow.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "sdl/SDLMixAudio.h"
#include "sdl/SDLMixMixer.h"
#include "ui/PadNames.h"
#include "resources/Resources.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace game
{

LuaUiScene::LuaUiScene(std::string scriptPath)
	: scriptPath_(std::move(scriptPath))
{
}

LuaUiScene::~LuaUiScene() = default;

void LuaUiScene::changeScene(const std::string &name)
{
	if(isFinished()){
		return;
	}
	auto next = SceneRegistry::create(name);
	if(!next){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "scene.change: unknown scene: %s", name.c_str());
		return;
	}
	getHost().registerNextScene(std::move(next));
	finish();
}

void LuaUiScene::onCreate(uint32_t tick)
{
	Scene::onCreate(tick);
	auto &window = vulkanWindow(*this);
	window.setClearColor(0.0f, 0.0f, 0.0f);
	// このシーンが使うデータの読み込み一覧(スクリプトと同じ名前の ".assets.lua")があれば、先に全部読む
	resources().loadManifest(ResourcePaths::assetsManifestFor(scriptPath_));
	ctx_ = std::make_unique<ui::UiContext>(window, getResources(), resources());
	ui::UiScript::Callbacks callbacks;
	callbacks.changeScene = [this](const std::string &name){ changeScene(name); };
	callbacks.quit = [this]{ quit(); };
	callbacks.command = [this](const std::string &name, double value){ onCommand(name, value); };
	callbacks.playSound = [this](const std::string &path){
		if(const auto sound = resources().sound(path)){
			getApplication().getMixer().playSound(*sound);
		}
	};
	script_ = std::make_unique<ui::UiScript>(*ctx_, std::move(callbacks));
	script_->load(scriptPath_);
	automation_ = DebugAutomation::fromEnv();
	startTick_ = lastTick_ = tick;
}

void LuaUiScene::dispatch(const SDL_Event &event)
{
	if(!script_){
		return;
	}
	switch(event.type){
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F5 && !event.key.repeat){
			reloadRequested_ = true; // スクリプトの読み直し(次のonIdleで)
		}
		else if(!event.key.repeat){
			script_->onKey(SDL_GetKeyName(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
		}
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP: {
		const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
		// 十字キーは、左スティックと合わせて、PadNavigatorで(繰り返しつきで)渡す
		if(button != SDL_GAMEPAD_BUTTON_DPAD_UP && button != SDL_GAMEPAD_BUTTON_DPAD_DOWN
			&& button != SDL_GAMEPAD_BUTTON_DPAD_LEFT && button != SDL_GAMEPAD_BUTTON_DPAD_RIGHT){
			const char *name = ui::padButtonName(button);
			if(*name){
				script_->onKey(name, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
			}
		}
		break;
	}
	case SDL_EVENT_MOUSE_MOTION: {
		const auto [x, y] = windowToScreen(getWindow(), *ctx_, event.motion.x, event.motion.y);
		script_->onMouseMove(x, y);
		break;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const auto [x, y] = windowToScreen(getWindow(), *ctx_, event.button.x, event.button.y);
		script_->onMouseButton(event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
		break;
	}
	default:
		break;
	}
}

bool LuaUiScene::onIdle(uint32_t tick)
{
	Scene::onIdle(tick);
	const bool running = true; // 毎フレーム再描画する(点滅・移動のため。falseだと、次のイベントまで再描画が止まる)
	if(isFinished() || !script_){
		return running;
	}
	// 動作確認用の環境変数(DebugAutomation参照): キー(AUTOKEY)・マウス(AUTOMOUSE)・読み直し(AUTORELOAD)。起点は、シーン開始(読み直しで更新)
	const int elapsedMs = static_cast<int>(tick - startTick_);
	while(const std::string *key = automation_.nextKey(elapsedMs)){
		script_->onKey(*key, true);
		script_->onKey(*key, false);
		if(isFinished()){
			return running;
		}
	}
	if(DebugAutomation::Mouse mouse; automation_.takeMouse(elapsedMs, mouse)){
		DebugAutomation::sendMouse(*script_, mouse);
	}
	if(automation_.takeReload(elapsedMs)){
		reloadRequested_ = true;
	}
	if(reloadRequested_){
		reloadRequested_ = false;
		SDL_Log("ui: reloading %s", scriptPath_.c_str());
		getResources().reload(); // 文字列・フォントの定義(lang/*.lua)も読み直す
		script_->load(scriptPath_);
		startTick_ = tick;
	}
	const float dt = std::min(static_cast<float>(tick - lastTick_) * 0.001f, 0.1f);
	lastTick_ = tick;
	padNavigator_.poll(getResources(), *script_, tick);
	if(isFinished()){
		return running;
	}
	script_->update(dt, static_cast<float>(tick - startTick_) * 0.001f);
	onFrame(tick, dt);
	if(isFinished()){
		return running;
	}
	script_->draw();
	swap();
	return running;
}

} // namespace game
