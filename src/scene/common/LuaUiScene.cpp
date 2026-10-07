#include "scene/common/LuaUiScene.h"
#include "app/Application.h"
#include "scene/SceneHost.h"
#include "scene/SceneRegistry.h"
#include "sdl/SDLVulkanWindow.h"
#include "resources/ResourceSet.h"
#include "sdl/SDLGamepad.h"
#include "sdl/SDLMixAudio.h"
#include "sdl/SDLMixMixer.h"
#include "ui/PadNames.h"
#include "resources/Resources.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

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
	auto &window = static_cast<SDL_::VulkanWindow &>(getWindow());
	window.setClearColor(0.0f, 0.0f, 0.0f);
	// このシーンが使うデータの読み込み一覧(スクリプトと同じ名前の ".assets.lua")があれば、先に全部読む
	{
		std::string manifest = scriptPath_;
		const auto dot = manifest.rfind(".lua");
		if(dot != std::string::npos){
			manifest.replace(dot, 4, ".assets.lua");
			resources().loadManifest(manifest);
		}
	}
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
	startTick_ = lastTick_ = tick;
}

void LuaUiScene::toScreen(float windowX, float windowY, float &x, float &y)
{
	const auto size = getWindow().getSize();
	x = windowX / static_cast<float>(std::max(size.getX(), 1)) * ctx_->screenWidth();
	y = windowY / static_cast<float>(std::max(size.getY(), 1)) * ctx_->screenHeight();
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
		float x, y;
		toScreen(event.motion.x, event.motion.y, x, y);
		script_->onMouseMove(x, y);
		break;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		float x, y;
		toScreen(event.button.x, event.button.y, x, y);
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
	// 動作確認用: VULKAN_AUTOKEY="キー名@ミリ秒" で、シーン開始からその時間後に、キーが押されたことにする(例: Return@3000)。
	// VULKAN_AUTORELOAD=ミリ秒 で、その時間後にスクリプトを読み直す(F5と同じ)
	static const char *autoKey = SDL_getenv("VULKAN_AUTOKEY");
	static const int autoReload = SDL_getenv("VULKAN_AUTORELOAD") ? SDL_atoi(SDL_getenv("VULKAN_AUTORELOAD")) : -1;
	if(autoKey){
		// "Right@1000,Down@1500" のように、カンマで区切って複数指定できる(時刻の昇順で)
		std::vector<std::pair<std::string, int>> keys;
		for(size_t start = 0; start < std::strlen(autoKey);){
			size_t end = std::string(autoKey).find(',', start);
			end = end == std::string::npos ? std::strlen(autoKey) : end;
			const std::string spec(autoKey + start, end - start);
			const auto at = spec.find('@');
			if(at != std::string::npos){
				keys.emplace_back(spec.substr(0, at), std::atoi(spec.c_str() + at + 1));
			}
			start = end + 1;
		}
		while(autoKeyDone_ < keys.size() && static_cast<int>(tick - startTick_) >= keys[autoKeyDone_].second){
			const std::string key = keys[autoKeyDone_++].first;
			script_->onKey(key, true);
			script_->onKey(key, false);
			if(isFinished()){
				return running;
			}
		}
	}
	// VULKAN_AUTOMOUSE="x,y@ミリ秒" で、シーン開始からその時間後に、マウスが論理画面の(x, y)へ動いたことにする。"x,y@ミリ秒,click" なら、そこでクリックも
	static const char *autoMouse = SDL_getenv("VULKAN_AUTOMOUSE");
	if(autoMouse && !autoMouseDone_){
		float mx = 0.0f, my = 0.0f;
		int at = 0;
		if(std::sscanf(autoMouse, "%f,%f@%d", &mx, &my, &at) == 3 && static_cast<int>(tick - startTick_) >= at){
			autoMouseDone_ = true;
			script_->onMouseMove(mx, my);
			if(std::strstr(autoMouse, ",click")){
				script_->onMouseButton(1, true, mx, my);
				script_->onMouseButton(1, false, mx, my);
			}
		}
	}
	if(autoReload >= 0 && !autoReloadDone_ && static_cast<int>(tick - startTick_) >= autoReload){
		autoReloadDone_ = true;
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
	onFrame(tick);
	if(isFinished()){
		return running;
	}
	script_->draw();
	swap();
	return running;
}

} // namespace game
