#include "scene/game/GameScene.h"
#include "geo/Calculator.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "sdl/SDLGamepad.h"
#include "resources/Resources.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include "ui/PadNames.h"
#include "scene/character/CharacterList.h"
#include "scene/characterselect/CharacterSelectScene.h"
#include "scene/common/ModelFactory.h"
#include "scene/game/GameSession.h"
#include "scene/SceneHost.h"
#include "scene/SceneRegistry.h"
#include "vk/PrimitiveMeshes.h"
#include "vk/VulkanMath.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace game
{

namespace
{
constexpr float kPi = 3.14159265f;

SDL_::VulkanWindow &vulkanWindow(Scene &scene)
{
	return static_cast<SDL_::VulkanWindow &>(scene.getWindow());
}

// 角度aをtargetへ、最短の向きで最大maxStepだけ近づける
float approachAngle(float a, float target, float maxStep)
{
	float diff = std::fmod(target - a + kPi, 2.0f * kPi);
	if(diff < 0.0f){
		diff += 2.0f * kPi;
	}
	diff -= kPi;
	return a + std::clamp(diff, -maxStep, maxStep);
}
}

namespace
{
// モーションのパス(game.assets.lua と同じパス)
constexpr const char *kMotionPaths[] = {
	"res/motion/Standing Idle.vrma",   // 立ち
	"res/motion/Walking.vrma",         // 歩き(左スティックを倒しきらない)
	"res/motion/Slow Run.vrma",        // 左スティックを最大に倒す
	"res/motion/Fast Run.vrma",        // Aボタンを押しっぱなし + 左スティックを最大に倒す
	"res/motion/Climbing Slope.vrma",  // 歩いて登れない少し急な坂へ進む(ゆっくり登る)
	"res/motion/Stand To Roll.vrma",   // Aボタン単押し
	"res/motion/Punching Right.vrma",  // R1
	"res/motion/Punching Left.vrma",   // L1
	"res/motion/Mma Kick Right High.vrma", // R2
	"res/motion/Roundhouse Kick.vrma",    // L2
	"res/motion/Standing To Crouched.vrma", // 坂を登り始める(しゃがむ)
	"res/motion/Crouch To Stand.vrma",      // 坂を登り終わる(立ち上がる)
};
}

GameScene::GameScene() = default;

GameScene::~GameScene() = default;

void GameScene::toScreen(float windowX, float windowY, float &x, float &y)
{
	const auto size = getWindow().getSize();
	x = windowX / static_cast<float>(std::max(size.getX(), 1)) * uiContext_->screenWidth();
	y = windowY / static_cast<float>(std::max(size.getY(), 1)) * uiContext_->screenHeight();
}

void GameScene::setPaused(bool paused)
{
	if(paused == paused_ || !hud_){
		return;
	}
	paused_ = paused;
	SDL_SetWindowRelativeMouseMode(vulkanWindow(*this).get(), !paused); // ポーズ中は、マウスカーソルでメニューを選ぶ
	if(paused){
		padNavigator_.reset();
		uiContext_->world().sensitivity = sensitivity_;
		pauseMenu_->load(kPauseScript); // 開くたびに、読み直して初期状態から(F5の読み直しも兼ねる)
	}
}

void GameScene::command(const std::string &name, double value)
{
	if(name == "resume"){
		setPaused(false);
	}
	else if(name == "respawn"){ // 最初の位置へ戻る(地形の端にはまって動けなくなったとき用)
		playerX_ = startX_;
		playerZ_ = startZ_;
		heading_ = 0.0f;
		motion_ = MotionIdle;
		motionTime_ = 0.0f;
		appliedMotion_ = -1; // フェードしない(急に位置が変わるので)
		fadeDuration_ = 0.0f;
		if(player_){
			player_->resetPhysics(); // 髪やスカートの揺れを、元の位置へ
		}
		setPaused(false);
	}
	else if(name == "sensitivity"){
		sensitivity_ = std::clamp(static_cast<float>(value), 0.1f, 4.0f);
		uiContext_->world().sensitivity = sensitivity_;
	}
	else{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "game.command: unknown command: %s", name.c_str());
	}
}

void GameScene::dispatch(const SDL_Event &event)
{
	switch(event.type){
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat){
			setPaused(!paused_);
		}
		else if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F5 && !event.key.repeat){
			reloadHud_ = true; // HUDのスクリプトの読み直し(次のonIdleで。ポーズ画面は、開くたびに読み直す)
		}
		else if(!event.key.repeat){
			if(paused_ && pauseMenu_){
				pauseMenu_->onKey(SDL_GetKeyName(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
			}
			else if(hud_){
				hud_->onKey(SDL_GetKeyName(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
			}
		}
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP: {
		const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
		if(button == SDL_GAMEPAD_BUTTON_START){
			if(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN){
				setPaused(!paused_);
			}
			break;
		}
		const char *name = ui::padButtonName(button);
		if(*name && button != SDL_GAMEPAD_BUTTON_DPAD_UP && button != SDL_GAMEPAD_BUTTON_DPAD_DOWN
			&& button != SDL_GAMEPAD_BUTTON_DPAD_LEFT && button != SDL_GAMEPAD_BUTTON_DPAD_RIGHT){
			const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
			if(paused_ && pauseMenu_){
				pauseMenu_->onKey(name, down);
			}
			else if(hud_){
				hud_->onKey(name, down);
			}
		}
		break;
	}
	case SDL_EVENT_MOUSE_MOTION: {
		if(paused_){
			float x, y;
			toScreen(event.motion.x, event.motion.y, x, y);
			pauseMenu_->onMouseMove(x, y);
			break;
		}
		// 視点の回転(マウスはウィンドウに取り込み済み): 右へ動かすと右を向く、下へ動かすと見下ろす
		const float kRadiansPerPixel = 0.003f * sensitivity_;
		cameraYaw_ -= event.motion.xrel * kRadiansPerPixel;
		cameraPitch_ = std::clamp(cameraPitch_ + event.motion.yrel * kRadiansPerPixel, kMinPitch, 1.3f);
		break;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if(paused_){
			float x, y;
			toScreen(event.button.x, event.button.y, x, y);
			pauseMenu_->onMouseButton(event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
		}
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		if(!paused_){
			cameraDistance_ = std::clamp(cameraDistance_ * std::exp(-event.wheel.y * 0.1f), minCameraDistance_, maxCameraDistance_);
		}
		break;
	default:
		break;
	}
}

void GameScene::onCreate(uint32_t tick)
{
	Scene::onCreate(tick);
	settings_ = loadGameSettings(); // 速さ・カメラ・クロスフェードなどの調整値(res/lua/data/game_settings.lua。F5で読み直す)
	auto &window = vulkanWindow(*this);
	auto &res = getResources();
	window.setScreenSize(static_cast<float>(res.getScreenWidth()), static_cast<float>(res.getScreenHeight()));
	window.setClearColor(0.55f, 0.7f, 0.9f);

	// ライティング: 軽さを優先して、シャドウマップは使わず、足元の丸い影にする。点光源は無し
	window.setShadowMapsEnabled(false);
	window.clearPointLights();
	window.setAmbientEnvironment(true, geo::Vector3f(0.36f, 0.32f, 0.27f), 0.0f, 0.5f, 0.6f);
	window.setLight(geo::Vector3f(-0.4f, -1.0f, -0.6f), 0.7f, 0.7f, 0.7f, 0.4f);
	blob_ = std::make_unique<BlobShadow>(window);
	// 地面: 設定(field_settings.lua)のフィールドを読む。無ければ、既定のタイル(0番)で埋めた平面
	const field::FieldSettings settings = field::loadFieldSettings();
	tiles_ = field::loadTileDefs(settings.tiles);
	if(tiles_.empty()){
		tiles_.push_back(field::TileDef{"ground", 'g', 96, 160, 72, ""});
	}
	if(!map_.load(ResourcePaths::resource(settings.field.c_str()), tiles_)){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load the field: %s (using a flat default)", settings.field.c_str());
	}
	fieldRenderer_ = std::make_unique<FieldRenderer>(window, map_, tiles_);
	propRenderer_ = std::make_unique<PropRenderer>(window, resources());
	for(const auto &prop : map_.props()){
		propRenderer_->preload(map_.propName(prop.prop)); // 置物のモデルは、最初の描画ではなく、ここで読む
	}
	movementRules_ = field::MovementRules::fromTiles(tiles_, settings.maxSlope, settings.maxClimbSlope);
	propCollision_.build(map_, [this](const std::string &name){ return propRenderer_->footprint(name); });
	playerX_ = fieldWidth() * 0.5f; // フィールドの真ん中から始める
	playerZ_ = fieldDepth() * 0.5f;
	startX_ = playerX_;
	startZ_ = playerZ_;

	// プレイヤー: キャラクタ選択画面で選んだキャラ(GameSession)。選択画面がGPU上に作ったモデルがあればそれを引き継ぎ、無ければ(データは読み込み済みなら
	// キャッシュから)ここで作る。シーンの破棄で、まとめて手放される
	resources().loadManifest(kAssetManifest);
	GameSession &session = GameSession::instance();
	if(session.character.empty()){
		const auto list = loadCharacterList(CharacterSelectScene::kCharacterList); // 選択画面を通らずに始まったときは、一覧の先頭
		session.character = list.empty() ? std::string() : list.front().model;
	}
	player_ = std::move(session.model);
	if(!player_ && !session.character.empty()){
		player_ = createVulkanModel(window, resources(), session.character);
	}
	if(!player_){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create the player.");
		quit();
		return;
	}
	{
		float top = 0.0f;
		for(const auto &vertex : player_->data().vertices){
			top = std::max(top, vertex.position[1]);
		}
		applyModelHeight(top);
	}
	player_->setAmbientBoost(0.0f);
	if(player_->skeleton()){
		for(int i = 0; i < MotionCount; ++i){
			if(const auto animation = resources().animation(kMotionPaths[i])){
				motions_[i] = model::VrmaPlayer::create(animation, player_->data(), *player_->skeleton(), player_->morphs());
				if(motions_[i]){
					motions_[i]->setInPlace(true); // 前へ進むのはキャラの位置(playerX_/Z_)で行う。モーションは、その場の動きだけ
				}
			}
		}
	}
	playerY_ = map_.heightAt(playerX_, playerZ_);
	playerTransform_.setPos(geo::Vector3f(playerX_, playerY_, playerZ_));
	playerTransform_.setScale(geo::Vector3f(1.0f, 1.0f, 1.0f));

	// HUD: Luaのウィジェット。スクリプトの読み込み一覧(hud.assets.lua)があれば、先に読む
	resources().loadManifest("res/lua/ui/hud.assets.lua");
	uiContext_ = std::make_unique<ui::UiContext>(window, getResources(), resources());
	ui::UiScript::Callbacks callbacks;
	callbacks.quit = [this]{ quit(); };
	callbacks.command = [this](const std::string &name, double value){ command(name, value); };
	hud_ = std::make_unique<ui::UiScript>(*uiContext_, std::move(callbacks));
	hud_->load(kHudScript);
	// ポーズ画面(開くときにload)
	ui::UiScript::Callbacks pauseCallbacks;
	pauseCallbacks.quit = [this]{ quit(); };
	pauseCallbacks.command = [this](const std::string &name, double value){ command(name, value); };
	pauseCallbacks.changeScene = [this](const std::string &name){
		if(auto next = SceneRegistry::create(name)){
			getHost().registerNextScene(std::move(next));
			finish();
		}
	};
	pauseMenu_ = std::make_unique<ui::UiScript>(*uiContext_, std::move(pauseCallbacks));

	// 動作確認用: VULKAN_PITCH=ラジアン で、カメラの縦の角度の初期値(例: -0.6 で下から見上げる、-1.57 で真上)
	if(const char *pitch = SDL_getenv("VULKAN_PITCH")){
		cameraPitch_ = std::clamp(static_cast<float>(SDL_atof(pitch)), kMinPitch, 1.3f);
	}
	// 動作確認用: VULKAN_YAW=ラジアン で、カメラの水平の角度の初期値(例: 1.57 で横から)
	if(const char *yaw = SDL_getenv("VULKAN_YAW")){
		cameraYaw_ = static_cast<float>(SDL_atof(yaw));
	}
	// マウスをウィンドウに取り込んで、視点の回転に使う
	SDL_SetWindowRelativeMouseMode(window.get(), true);
	lastTick_ = tick;
	startTick_ = tick;
}

void GameScene::onDestroy(uint32_t tick)
{
	SDL_SetWindowRelativeMouseMode(vulkanWindow(*this).get(), false);
	Scene::onDestroy(tick);
}

void GameScene::capturePose(const model::Skeleton &skeleton, Pose &pose)
{
	pose.rotations.resize(skeleton.boneCount());
	pose.translations.resize(skeleton.boneCount());
	for(size_t i = 0; i < skeleton.boneCount(); ++i){
		pose.rotations[i] = skeleton.boneRotation(static_cast<int>(i));
		pose.translations[i] = skeleton.boneTranslation(static_cast<int>(i));
	}
}

bool GameScene::stepMove(float dirX, float dirZ, float distance, float maxSlope)
{
	const float beforeX = playerX_, beforeZ = playerZ_;
	// フィールドの縁: 外へは出られない(壁に沿っては滑れる。軸ごとに止める)
	const float targetX = std::clamp(playerX_ + dirX * distance, settings_.player.radius, fieldWidth() - settings_.player.radius);
	const float targetZ = std::clamp(playerZ_ + dirZ * distance, settings_.player.radius, fieldDepth() - settings_.player.radius);
	// 地面: 歩けないタイル(水)と急な勾配へは進めない(沿って滑れる)
	field::moveOnField(map_, movementRules_, settings_.player.radius, playerX_, playerZ_, targetX, targetZ, maxSlope);
	// 置物: めり込んだら、外へ押し出す(壁に沿って滑れる)。押し出しでフィールドの外・水・急な所へ出たら、押し出す前へ戻す
	float pushedX = playerX_, pushedZ = playerZ_;
	if(propCollision_.resolve(pushedX, pushedZ, settings_.player.radius)){
		pushedX = std::clamp(pushedX, settings_.player.radius, fieldWidth() - settings_.player.radius);
		pushedZ = std::clamp(pushedZ, settings_.player.radius, fieldDepth() - settings_.player.radius);
		float checkX = playerX_, checkZ = playerZ_;
		if(field::moveOnField(map_, movementRules_, settings_.player.radius, checkX, checkZ, pushedX, pushedZ) && checkX == pushedX && checkZ == pushedZ){
			playerX_ = pushedX;
			playerZ_ = pushedZ;
		}
	}
	return playerX_ != beforeX || playerZ_ != beforeZ;
}

// アクション(転がる・攻撃)を始める。モーションが無ければ何もしない。転がるときは、入力の向き(dirX, dirZ。無入力なら0)へ向いてから転がる
void GameScene::startAction(int motion, float dirX, float dirZ)
{
	if(!motions_[motion]){
		return;
	}
	if(motion == MotionRoll){
		if(dirX != 0.0f || dirZ != 0.0f){
			heading_ = std::atan2(dirX, dirZ);
		}
		rollDirX_ = std::sin(heading_);
		rollDirZ_ = std::cos(heading_);
	}
	motion_ = motion;
	motionTime_ = 0.0f;
}

// 入力に合わせてプレイヤーを動かし、モーションを当てる:
//   左スティックを倒しきらない: 歩き。最大(キーボードはShift): Slow Run。Aボタン(キーボードはSpace)を押しっぱなしで最大: Fast Run
//   歩いて登れない少し急な坂(maxSlope〜maxClimbSlope)へ進む: Climbing Slope(ゆっくり登る)
//   Aボタンの単押し: Stand To Roll。R1: Punching Right、R2: Mma Kick Right High、L1: Punching Left、L2: Roundhouse Kick(キーボードは X V Z C)
//   アクションは、終わるまで他の操作を受けない(転がるときだけ、前へ進む)
void GameScene::updatePlayer(float dt, uint32_t tick)
{
	const bool *keys = SDL_GetKeyboardState(nullptr);
	float right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
	float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
	bool run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
	bool actionDown = keys[SDL_SCANCODE_SPACE];
	bool attack[4] = {keys[SDL_SCANCODE_X] != 0, keys[SDL_SCANCODE_V] != 0, keys[SDL_SCANCODE_Z] != 0, keys[SDL_SCANCODE_C] != 0}; // R1 R2 L1 L2
	// ゲームパッド: 左スティックで移動(傾きの分だけ進む)、右スティックでカメラ(視点)を回す
	if(const auto pad = getResources().getGamepad()){
		float lx, ly, rx, ry;
		pad->leftStick(lx, ly);
		pad->rightStick(rx, ry);
		right += lx;
		forward -= ly; // スティックは下が+
		run = run || std::sqrt(lx * lx + ly * ly) >= settings_.input.runStick;
		cameraYaw_ -= rx * settings_.camera.yawSpeed * sensitivity_ * dt;
		cameraPitch_ = std::clamp(cameraPitch_ + ry * settings_.camera.pitchSpeed * sensitivity_ * dt, kMinPitch, 1.3f);
		actionDown = actionDown || pad->button(SDL_GAMEPAD_BUTTON_SOUTH);
		attack[0] = attack[0] || pad->button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
		attack[1] = attack[1] || pad->axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > settings_.input.triggerOn;
		attack[2] = attack[2] || pad->button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		attack[3] = attack[3] || pad->axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > settings_.input.triggerOn;
	}
	// 動作確認用: VULKAN_AUTOWALK=1 で、常に前へ進む入力にする。=2 なら、常に右へ(横から見るため)
	static const char *autoWalk = SDL_getenv("VULKAN_AUTOWALK");
	if(autoWalk){
		forward = autoWalk[0] == '2' ? 0.0f : 1.0f;
		right = autoWalk[0] == '2' ? 1.0f : right;
	}
	// 動作確認用: VULKAN_AUTORUN=1 で、スティックを最大に倒した入力にする。=2 なら、Aボタンも押しっぱなし(Fast Run)
	static const char *autoRun = SDL_getenv("VULKAN_AUTORUN");
	if(autoRun){
		run = true;
		actionDown = actionDown || autoRun[0] == '2';
	}
	// 動作確認用: VULKAN_AUTOACTION="roll|pr|pl|kh|rh@ミリ秒,..."(時刻の昇順)で、そのアクションのボタンが押されたことにする
	static const char *autoAction = SDL_getenv("VULKAN_AUTOACTION");
	if(autoAction){
		const std::string all(autoAction);
		std::vector<std::pair<std::string, int>> actions;
		for(size_t start = 0; start < all.size();){
			size_t end = all.find(',', start);
			end = end == std::string::npos ? all.size() : end;
			const std::string spec = all.substr(start, end - start);
			const auto at = spec.find('@');
			if(at != std::string::npos){
				actions.emplace_back(spec.substr(0, at), std::atoi(spec.c_str() + at + 1));
			}
			start = end + 1;
		}
		while(autoActionDone_ < actions.size() && static_cast<int>(tick - startTick_) >= actions[autoActionDone_].second){
			const std::string &name = actions[autoActionDone_++].first;
			pendingAuto_ = name == "roll" ? MotionRoll : name == "pr" ? MotionPunchRight : name == "pl" ? MotionPunchLeft : name == "kh" ? MotionKickHigh : MotionRoundhouse;
		}
	}
	// カメラの前(水平)と右(水平)。カメラは注視点の(sin yaw, cos yaw)側にいる
	const float fx = -std::sin(cameraYaw_), fz = -std::cos(cameraYaw_);
	const float rx = std::cos(cameraYaw_), rz = -std::sin(cameraYaw_);
	float dx = fx * forward + rx * right;
	float dz = fz * forward + rz * right;
	const float length = std::sqrt(dx * dx + dz * dz);
	const float magnitude = std::min(length, 1.0f); // スティックを少しだけ倒したときは、ゆっくり進む(キーボードは常に1)
	if(length > 0.0f){
		dx /= length;
		dz /= length;
	}

	// ボタンの押し始め・離したとき(アクションの開始)
	const bool busy = isAction(motion_);
	if(actionDown){
		actionHeldTime_ += dt;
	}
	if(actionHeld_ && !actionDown){
		if(actionHeldTime_ < settings_.input.tapTime && !busy){
			startAction(MotionRoll, dx, dz); // 単押し
		}
		actionHeldTime_ = 0.0f;
	}
	actionHeld_ = actionDown;
	static const int kAttackMotions[4] = {MotionPunchRight, MotionKickHigh, MotionPunchLeft, MotionRoundhouse};
	for(int i = 0; i < 4; ++i){
		if(attack[i] && !actionPrev_[i] && !isAction(motion_)){
			startAction(kAttackMotions[i], 0.0f, 0.0f);
		}
		actionPrev_[i] = attack[i];
	}
	if(pendingAuto_ >= 0){
		if(!isAction(motion_)){
			startAction(pendingAuto_, dx, dz);
		}
		pendingAuto_ = -1;
	}

	// 動き: アクション中は、転がるときだけ前へ進む(坂の登り始め・終わりのモーションの間は、止まる)。それ以外は、入力の向きへ
	walking_ = false;
	int wanted = MotionIdle;
	if(isAction(motion_)){
		wanted = motion_;
		const auto &player = motions_[motion_];
		if(motion_ == MotionRoll){
			stepMove(rollDirX_, rollDirZ_, settings_.player.rollDistance / player->duration() * dt);
		}
		const float rate = motion_ == MotionCrouchEnter ? settings_.climb.enterSpeed : motion_ == MotionCrouchExit ? settings_.climb.exitSpeed : 1.0f;
		motionTime_ += dt * rate;
		if(motionTime_ >= player->duration()){
			if(motion_ == MotionCrouchEnter){
				climbing_ = true; // しゃがみ終わり: 登り始める
				climbRelease_ = 0.0f;
			}
			else if(motion_ == MotionCrouchExit){
				climbing_ = false; // 立ち上がり終わり
			}
			motion_ = MotionIdle; // 終わり。下で、立ち・歩き・走り・登りへ戻る
			motionTime_ = 0.0f;
			wanted = MotionIdle;
		}
	}
	if(!isAction(motion_)){
		float speed = 0.0f;
		const float climbLimit = movementRules_.maxClimbSlope * 1.05f;
		const float ahead = length > 0.0f ? field::slopeAhead(map_, playerX_, playerZ_, dx, dz) : 0.0f;
		if(climbing_){
			// 登っている間: 先が登れる坂のうちは、登り続ける。登れる坂でなくなって(止まる・平らになる)しばらくしたら、立ち上がる
			const bool keep = length > 0.0f && ahead > movementRules_.maxSlope * settings_.climb.exitSlopeRatio && ahead <= movementRules_.maxClimbSlope;
			climbRelease_ = keep ? 0.0f : climbRelease_ + dt;
			if(climbRelease_ >= settings_.climb.exitHold){
				climbRelease_ = 0.0f;
				if(motions_[MotionCrouchExit]){
					startAction(MotionCrouchExit, 0.0f, 0.0f);
				}
				else{
					climbing_ = false;
				}
			}
			else if(length > 0.0f){
				walking_ = true;
				wanted = MotionClimb;
				stepMove(dx, dz, settings_.player.climbSpeed * dt, climbLimit);
				heading_ = approachAngle(heading_, std::atan2(dx, dz), settings_.player.turnSpeed * dt);
				climbSlope_ += (std::max(ahead, 0.0f) - climbSlope_) * std::min(1.0f, 8.0f * dt);
			}
		}
		else if(length > 0.0f){
			walking_ = true;
			const bool fast = run && actionDown && actionHeldTime_ >= settings_.input.tapTime;
			// 向かう先が、歩いては登れない少し急な坂なら、しゃがんでから、ゆっくり登る(これより急な坂は、進めない)
			const bool climbable = motions_[MotionClimb] && ahead > movementRules_.maxSlope * settings_.player.climbEnter && ahead <= movementRules_.maxClimbSlope;
			if(climbable){
				climbSlope_ = ahead;
				if(motions_[MotionCrouchEnter]){
					startAction(MotionCrouchEnter, 0.0f, 0.0f);
					heading_ = std::atan2(dx, dz); // 坂の方を向いてしゃがむ
				}
				else{
					climbing_ = true;
				}
				walking_ = false;
			}
			else{
				if(run){
					wanted = fast ? MotionFastRun : MotionSlowRun;
					speed = fast ? settings_.player.fastRunSpeed : settings_.player.slowRunSpeed;
				}
				else{
					wanted = MotionWalk;
					speed = settings_.player.walkSpeed * magnitude;
				}
				if(!motions_[wanted]){ // 走りのモーションが読めなかったときは、歩きで
					wanted = MotionWalk;
				}
				if(!stepMove(dx, dz, speed * dt) && motions_[MotionClimb] && ahead > 0.0f){
					// 歩いては進めなかった(セルごとの勾配が、先の平均より急だったとき): 登れる坂なら、しゃがんでから登る
					if(stepMove(dx, dz, settings_.player.climbSpeed * dt, climbLimit)){
						climbSlope_ = ahead;
						if(motions_[MotionCrouchEnter]){
							startAction(MotionCrouchEnter, 0.0f, 0.0f);
						}
						else{
							climbing_ = true;
						}
					}
				}
				heading_ = approachAngle(heading_, std::atan2(dx, dz), settings_.player.turnSpeed * dt);
			}
		}
		if(!isAction(motion_)){
			// モーションの切り替え: ループの途中の位置(割合)を引き継ぐ(足の運びが飛ばないように)
			if(wanted != motion_ && motions_[wanted] && motions_[motion_]){
				motionTime_ = motionTime_ / motions_[motion_]->duration() * motions_[wanted]->duration();
			}
			motion_ = wanted;
		}
	}
	// 坂を登っているとき、キャラを坂に沿って(坂の角度の tiltFactor の割合だけ)後ろへ傾ける。なめらかに追いつく
	{
		const float target = motion_ == MotionClimb ? std::atan(climbSlope_) * settings_.climb.tiltFactor : 0.0f;
		tilt_ += (target - tilt_) * std::min(1.0f, settings_.climb.tiltSmooth * dt);
	}
	playerY_ = map_.heightAt(playerX_, playerZ_);
	playerTransform_.setPos(geo::Vector3f(playerX_, playerY_, playerZ_));
	playerTransform_.setRotation(geo::Quaternionf::createRotater(heading_, geo::Vector3f(0.0f, 1.0f, 0.0f)) * geo::Quaternionf::createRotater(-tilt_, geo::Vector3f(1.0f, 0.0f, 0.0f)));

	// モーション
	auto *skeleton = player_->skeleton();
	if(!skeleton){
		return;
	}
	auto *morphs = player_->morphs();
	skeleton->resetPose();
	if(morphs){
		morphs->resetWeights();
	}
	if(const auto &player = motions_[motion_]){
		if(!isAction(motion_)){
			motionTime_ = std::fmod(motionTime_ + dt, player->duration() + 1e-3f); // 立ち・歩き・走りは、ループ
		}
		player->apply(*skeleton, morphs, std::min(motionTime_, player->duration()));
	}
	// クロスフェード: モーションが切り替わった瞬間に、直前の姿勢を覚えて、新しい姿勢へ混ぜていく
	if(appliedMotion_ >= 0 && appliedMotion_ != motion_ && !lastPose_.rotations.empty()){
		fadeFrom_ = lastPose_;
		fadeTime_ = 0.0f;
		fadeDuration_ = isAction(motion_) ? settings_.fade.toAction : isAction(appliedMotion_) ? settings_.fade.fromAction : settings_.fade.locomotion;
	}
	appliedMotion_ = motion_;
	if(fadeTime_ < fadeDuration_ && fadeFrom_.rotations.size() == skeleton->boneCount()){
		fadeTime_ += dt;
		const float t = std::clamp(fadeTime_ / fadeDuration_, 0.0f, 1.0f);
		const float alpha = t * t * (3.0f - 2.0f * t); // なめらかに始まり、なめらかに終わる
		for(size_t i = 0; i < skeleton->boneCount(); ++i){
			const int bone = static_cast<int>(i);
			const model::Quat &toRotation = skeleton->boneRotation(bone);
			const model::Vec3 &toTranslation = skeleton->boneTranslation(bone);
			const model::Vec3 &fromTranslation = fadeFrom_.translations[i];
			skeleton->setBoneRotation(bone, model::Quat::slerp(fadeFrom_.rotations[i], toRotation, alpha));
			skeleton->setBoneTranslation(bone, {fromTranslation.x + (toTranslation.x - fromTranslation.x) * alpha,
				fromTranslation.y + (toTranslation.y - fromTranslation.y) * alpha, fromTranslation.z + (toTranslation.z - fromTranslation.z) * alpha});
		}
	}
	capturePose(*skeleton, lastPose_);
	player_->updatePose(dt);
}

void GameScene::drawScene(const geo::Matrix4x4f &viewProj)
{
	auto &window = vulkanWindow(*this);
	fieldRenderer_->draw(window, viewProj);
	propRenderer_->draw(map_, viewProj);
	// プレイヤーと、足元の丸い影
	window.draw(player_, viewProj, playerTransform_.getMatrix());
	blob_->draw(window, viewProj, playerX_, playerY_, playerZ_, 0.5f, 0.55f);
}

// HUDへ、ゲームの状態を渡して、スクリプトのupdateを進める(描画はdrawHudで)
void GameScene::updateHud(float dt, uint32_t tick)
{
	(void)tick;
	if(reloadHud_){
		reloadHud_ = false;
		SDL_Log("hud: reloading %s", kHudScript);
		getResources().reload();
		hud_->load(kHudScript);
		reloadSettings();
	}
	if(dt > 0.0f){
		const float instant = 1.0f / dt;
		fps_ = fps_ <= 0.0f ? instant : fps_ + (instant - fps_) * 0.1f; // 少しならして、数字がちらつかないように
	}
	auto &world = uiContext_->world();
	world.playerX = playerX_;
	world.playerZ = playerZ_;
	world.heading = heading_;
	world.cameraYaw = cameraYaw_;
	world.boundsMinX = 0.0f;
	world.boundsMinZ = 0.0f;
	world.boundsMaxX = fieldWidth();
	world.boundsMaxZ = fieldDepth();
	world.fps = fps_;
	hud_->update(dt, static_cast<float>(tick) * 0.001f);
}

void GameScene::reloadSettings()
{
	SDL_Log("game: reloading settings");
	settings_ = loadGameSettings();
	const field::FieldSettings fieldSettings = field::loadFieldSettings();
	movementRules_ = field::MovementRules::fromTiles(tiles_, fieldSettings.maxSlope, fieldSettings.maxClimbSlope);
	applyModelHeight(modelHeight_); // カメラの高さ・距離(初期値へ戻る)
}

void GameScene::applyModelHeight(float height)
{
	// モデルのメートルの単位で、背が極端に小さい/大きい(単位が違う・頂点が無い)ときは、基準の高さで
	const float reference = settings_.camera.referenceHeight;
	modelHeight_ = (height > 0.3f && height < 10.0f) ? height : reference;
	const float scale = modelHeight_ / reference;
	cameraHeight_ = settings_.camera.height * scale;
	minEyeHeight_ = settings_.camera.minEyeHeight * scale;
	headTop_ = settings_.camera.headTop * scale;
	minCameraDistance_ = settings_.camera.minDistance * scale;
	maxCameraDistance_ = settings_.camera.maxDistance * scale;
	cameraDistance_ = settings_.camera.distance * scale;
	SDL_Log("Player height %.2f m (camera height %.2f, distance %.2f)", modelHeight_, cameraHeight_, cameraDistance_);
}

void GameScene::computeCamera(float dt, geo::Vector3f &eye, geo::Vector3f &lookAt, geo::Vector3f &up)
{
	const float sinYaw = std::sin(cameraYaw_), cosYaw = std::cos(cameraYaw_);
	const geo::Vector3f head(playerX_, playerY_ + cameraHeight_, playerZ_);
	// 目の高さが minEyeHeight_ になる角度(これより下を向こうとすると、地面に潜る)
	const float groundPitch = std::asin(std::clamp((minEyeHeight_ - cameraHeight_) / cameraDistance_, -1.0f, 1.0f));
	if(cameraPitch_ >= groundPitch){
		// 通常: 頭を中心に、後ろ(yaw/pitch/distance)から見る
		eye = head + geo::Vector3f(std::cos(cameraPitch_) * sinYaw, std::sin(cameraPitch_), std::cos(cameraPitch_) * cosYaw) * cameraDistance_;
		lookAt = head;
	}
	else{
		// 地面に潜りそうなとき(u: 0〜1で進む): まず、地面すれすれの低い位置のまま、体のすぐ近く(bodyDistance)まで寄る(前半。見る先は頭のまま)。
		// 近くへ寄ってから、体に沿った弧を描いて、頭のてっぺんの上まで上がる(後半)。上がるにつれて、見る先は頭から真上へ移り(終盤に大きく)、最後は真上を向く
		const float u = std::clamp((groundPitch - cameraPitch_) / (groundPitch - kMinPitch), 0.0f, 1.0f);
		const float approach = std::clamp(u / settings_.camera.approachFraction, 0.0f, 1.0f); // 寄る進み具合
		const float rise = std::clamp((u - settings_.camera.approachFraction) / (1.0f - settings_.camera.approachFraction), 0.0f, 1.0f); // 上がる進み具合
		const float theta = rise * 1.5707963f;
		const float startDistance = std::cos(groundPitch) * cameraDistance_;
		const float bodyDistance = std::min(settings_.camera.bodyDistance * modelHeight_ / settings_.camera.referenceHeight, startDistance);
		const float easedApproach = approach * approach * (3.0f - 2.0f * approach);
		const float horizontal = (startDistance + (bodyDistance - startDistance) * easedApproach) * std::cos(theta);
		const float height = playerY_ + minEyeHeight_ + (headTop_ - minEyeHeight_) * std::sin(theta);
		eye = geo::Vector3f(playerX_ + sinYaw * horizontal, height, playerZ_ + cosYaw * horizontal);
		const float look = rise * rise; // 見る先が頭から離れるのは、上がる後半の終盤から(それまでは、体が視界に残る)
		lookAt = head + (eye + geo::Vector3f(0.0f, 4.0f, 0.0f) - head) * look;
	}
	// 地形: 頭からカメラへの線が地面(丘・坂)に当たるなら、当たる手前まで引き寄せる。縮むときはすぐ、戻るときはなめらかに(カメラが震えないよう)。
	// 引き寄せたあとも、カメラの下の地面から minEyeHeight_ 以上は高くする
	{
		const geo::Vector3f arm = eye - head;
		const float armLength = std::sqrt(arm.getX() * arm.getX() + arm.getY() * arm.getY() + arm.getZ() * arm.getZ());
		float wanted = 1.0f;
		if(armLength > 1e-4f){
			const float origin[3] = {head.getX(), head.getY(), head.getZ()};
			const float direction[3] = {arm.getX() / armLength, arm.getY() / armLength, arm.getZ() / armLength};
			float t = 0.0f, hit[3];
			if(map_.raycast(origin, direction, armLength, t, hit)){
				wanted = std::clamp((t - minEyeHeight_) / armLength, std::min(settings_.camera.minArm / armLength, 1.0f), 1.0f);
			}
		}
		if(wanted < cameraArm_){
			cameraArm_ = wanted;
		}
		else{
			cameraArm_ += (wanted - cameraArm_) * std::min(1.0f, settings_.camera.armRecover * dt);
		}
		eye = head + arm * cameraArm_;
		const float floorY = map_.heightAt(eye.getX(), eye.getZ()) + minEyeHeight_;
		if(eye.getY() < floorY){
			eye = geo::Vector3f(eye.getX(), floorY, eye.getZ());
		}
	}
	// 上向き: 見ている向きの仰角から。真上を見ても、(正面の逆の向きが画面の上になるよう)つぶれない
	const geo::Vector3f forward = geo::Vector3f::normalize(lookAt - eye);
	const float elevation = std::asin(std::clamp(forward.getY(), -1.0f, 1.0f));
	const geo::Vector3f ahead(-sinYaw, 0.0f, -cosYaw); // 水平の正面(カメラはプレイヤーの(sin, cos)側にいる)
	up = ahead * -std::sin(elevation) + geo::Vector3f(0.0f, std::cos(elevation), 0.0f);
}

bool GameScene::onIdle(uint32_t tick)
{
	Scene::onIdle(tick);
	const bool running = true; // 毎フレーム再描画する(点滅・移動・アニメーションのため。falseだと、次のイベントまで待ってしまう)
	if(isFinished() || !player_){
		return running;
	}
	auto &window = vulkanWindow(*this);
	const float dt = std::min(static_cast<float>(tick - lastTick_) * 0.001f, 0.1f);
	lastTick_ = tick;
	// 動作確認用: VULKAN_AUTOPAUSE=ミリ秒 で、その時間後にポーズを開く。VULKAN_AUTOMOUSE="x,y@ミリ秒[,click]" は、LuaUiSceneと同じ(ポーズ中のメニューへ)
	static const char *autoPause = SDL_getenv("VULKAN_AUTOPAUSE");
	if(autoPause && !paused_ && !autoPauseDone_ && tick >= static_cast<uint32_t>(std::atoi(autoPause)) + startTick_){
		autoPauseDone_ = true;
		setPaused(true);
	}
	// 動作確認用: VULKAN_AUTORELOAD=ミリ秒 で、その時間後に、F5と同じ読み直しをする
	static const char *autoReload = SDL_getenv("VULKAN_AUTORELOAD");
	if(autoReload && !autoReloadDone_ && tick >= static_cast<uint32_t>(std::atoi(autoReload)) + startTick_){
		autoReloadDone_ = true;
		reloadHud_ = true;
	}
	static const char *autoMouse = SDL_getenv("VULKAN_AUTOMOUSE");
	if(autoMouse && paused_ && !autoMouseDone_){
		float mx = 0.0f, my = 0.0f;
		int at = 0;
		if(std::sscanf(autoMouse, "%f,%f@%d", &mx, &my, &at) == 3 && tick >= startTick_ + static_cast<uint32_t>(at)){
			autoMouseDone_ = true;
			pauseMenu_->onMouseMove(mx, my);
			if(std::strstr(autoMouse, ",click")){
				pauseMenu_->onMouseButton(1, true, mx, my);
				pauseMenu_->onMouseButton(1, false, mx, my);
			}
		}
	}
	if(!paused_){
		updatePlayer(dt, tick);
	}

	// 三人称のカメラ
	geo::Vector3f eye, lookAt, up;
	computeCamera(dt, eye, lookAt, up);
	const auto view = geo::createLookAt<float>(eye, lookAt, up);
	const auto proj = vk_::createPerspective(kPi / 3.0f, window.getScreenWidth(), window.getScreenHeight(), 0.1f, 200.0f);
	window.setCameraPosition(eye);

	drawScene(proj * view);
	updateHud(paused_ ? 0.0f : dt, tick);
	hud_->draw();
	if(paused_){
		padNavigator_.poll(getResources(), *pauseMenu_, tick);
		pauseMenu_->update(dt, static_cast<float>(tick) * 0.001f);
		if(isFinished()){
			return running; // 「タイトルへ」などで、次のシーンへ
		}
		pauseMenu_->draw();
	}
	swap();
	return running;
}

} // namespace game
