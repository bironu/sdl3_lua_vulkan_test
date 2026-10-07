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
constexpr const char *kWalkMotionPath = "res/motion/Walking.vrma";
constexpr const char *kIdleMotionPath = "res/motion/VRMA_01.vrma";
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
	playerX_ = fieldWidth() * 0.5f; // フィールドの真ん中から始める
	playerZ_ = fieldDepth() * 0.5f;

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
		if(const auto walk = resources().animation(kWalkMotionPath)){
			walkPlayer_ = model::VrmaPlayer::create(walk, player_->data(), *player_->skeleton(), player_->morphs());
			if(walkPlayer_){
				walkPlayer_->setInPlace(true); // 前へ進むのはキャラの位置(playerX_/Z_)で行う。モーションは足踏みだけ
			}
		}
		if(const auto idle = resources().animation(kIdleMotionPath)){
			idlePlayer_ = model::VrmaPlayer::create(idle, player_->data(), *player_->skeleton(), player_->morphs());
			if(idlePlayer_){
				idlePlayer_->setInPlace(true);
			}
		}
	}
	playerTransform_.setPos(geo::Vector3f(playerX_, 0.0f, playerZ_));
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

// 入力に合わせてプレイヤーを動かし、歩き/立ちのモーションを当てる
void GameScene::updatePlayer(float dt, uint32_t tick)
{
	const bool *keys = SDL_GetKeyboardState(nullptr);
	float right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
	float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
	// ゲームパッド: 左スティックで移動(傾きの分だけ進む)、右スティックでカメラ(視点)を回す
	if(const auto pad = getResources().getGamepad()){
		constexpr float kCameraYawSpeed = 2.6f;   // 右スティックを倒しきったときの、水平の回転の速さ(ラジアン/秒)
		constexpr float kCameraPitchSpeed = 1.6f; // 同、上下
		float lx, ly, rx, ry;
		pad->leftStick(lx, ly);
		pad->rightStick(rx, ry);
		right += lx;
		forward -= ly; // スティックは下が+
		cameraYaw_ -= rx * kCameraYawSpeed * sensitivity_ * dt;
		cameraPitch_ = std::clamp(cameraPitch_ + ry * kCameraPitchSpeed * sensitivity_ * dt, kMinPitch, 1.3f);
	}
	static const bool autoWalk = SDL_getenv("VULKAN_AUTOWALK") != nullptr; // 動作確認用: 常に前へ進む入力にする
	forward = autoWalk ? 1.0f : forward;
	// カメラの前(水平)と右(水平)。カメラは注視点の(sin yaw, cos yaw)側にいる
	const float fx = -std::sin(cameraYaw_), fz = -std::cos(cameraYaw_);
	const float rx = std::cos(cameraYaw_), rz = -std::sin(cameraYaw_);
	float dx = fx * forward + rx * right;
	float dz = fz * forward + rz * right;
	const float length = std::sqrt(dx * dx + dz * dz);
	walking_ = length > 0.0f;
	if(walking_){
		const float magnitude = std::min(length, 1.0f); // スティックを少しだけ倒したときは、ゆっくり進む(キーボードは常に1)
		dx /= length;
		dz /= length;
		// フィールドの縁: 外へは出られない(壁に沿っては滑れる。軸ごとに止める)
		playerX_ = std::clamp(playerX_ + dx * kWalkSpeed * magnitude * dt, kPlayerRadius, fieldWidth() - kPlayerRadius);
		playerZ_ = std::clamp(playerZ_ + dz * kWalkSpeed * magnitude * dt, kPlayerRadius, fieldDepth() - kPlayerRadius);
		heading_ = approachAngle(heading_, std::atan2(dx, dz), kTurnSpeed * dt);
	}
	playerTransform_.setPos(geo::Vector3f(playerX_, 0.0f, playerZ_));
	playerTransform_.setRotation(geo::Quaternionf::createRotater(heading_, geo::Vector3f(0.0f, 1.0f, 0.0f)));

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
	if(walking_ && walkPlayer_){
		walkTime_ = std::fmod(walkTime_ + dt, walkPlayer_->duration() + 1e-3f);
		walkPlayer_->apply(*skeleton, morphs, walkTime_);
	}
	else if(!walking_ && idlePlayer_){
		idleTime_ = std::fmod(idleTime_ + dt, idlePlayer_->duration() + 1e-3f);
		idlePlayer_->apply(*skeleton, morphs, idleTime_);
	}
	player_->updatePose(dt);
	(void)tick;
}

void GameScene::drawScene(const geo::Matrix4x4f &viewProj)
{
	auto &window = vulkanWindow(*this);
	fieldRenderer_->draw(window, viewProj);
	// プレイヤーと、足元の丸い影
	window.draw(player_, viewProj, playerTransform_.getMatrix());
	blob_->draw(window, viewProj, playerX_, 0.0f, playerZ_, 0.5f, 0.55f);
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

void GameScene::applyModelHeight(float height)
{
	// モデルのメートルの単位で、背が極端に小さい/大きい(単位が違う・頂点が無い)ときは、基準の高さで
	modelHeight_ = (height > 0.3f && height < 10.0f) ? height : kReferenceHeight;
	cameraHeight_ = kCameraHeightRatio * modelHeight_;
	minEyeHeight_ = kMinEyeHeightRatio * modelHeight_;
	headTop_ = kHeadTopRatio * modelHeight_;
	minCameraDistance_ = kMinDistanceRatio * modelHeight_;
	maxCameraDistance_ = kMaxDistanceRatio * modelHeight_;
	cameraDistance_ = kDefaultDistanceRatio * modelHeight_;
	SDL_Log("Player height %.2f m (camera height %.2f, distance %.2f)", modelHeight_, cameraHeight_, cameraDistance_);
}

void GameScene::computeCamera(geo::Vector3f &eye, geo::Vector3f &lookAt, geo::Vector3f &up) const
{
	const float sinYaw = std::sin(cameraYaw_), cosYaw = std::cos(cameraYaw_);
	const geo::Vector3f head(playerX_, cameraHeight_, playerZ_);
	// 目の高さが minEyeHeight_ になる角度(これより下を向こうとすると、地面に潜る)
	const float groundPitch = std::asin(std::clamp((minEyeHeight_ - cameraHeight_) / cameraDistance_, -1.0f, 1.0f));
	if(cameraPitch_ >= groundPitch){
		// 通常: 頭を中心に、後ろ(yaw/pitch/distance)から見る
		eye = head + geo::Vector3f(std::cos(cameraPitch_) * sinYaw, std::sin(cameraPitch_), std::cos(cameraPitch_) * cosYaw) * cameraDistance_;
		lookAt = head;
	}
	else{
		// 地面に潜りそうなとき: 地面すれすれの位置から、体に沿った(外へふくらんだ)弧を描いて、頭のてっぺんの上まで上がる。
		// 進むほど(u: 0〜1)、見る先は頭から真上へ移り(終盤に大きく)、最後は真上を向く
		const float u = std::clamp((groundPitch - cameraPitch_) / (groundPitch - kMinPitch), 0.0f, 1.0f);
		const float theta = u * 1.5707963f;
		const float startDistance = std::cos(groundPitch) * cameraDistance_;
		const float horizontal = startDistance * std::cos(theta);
		const float height = minEyeHeight_ + (headTop_ - minEyeHeight_) * std::sin(theta);
		eye = geo::Vector3f(playerX_ + sinYaw * horizontal, height, playerZ_ + cosYaw * horizontal);
		const float look = u * u; // 見る先が頭から離れるのは終盤から(途中までは、体が視界に残る)
		lookAt = head + (eye + geo::Vector3f(0.0f, 4.0f, 0.0f) - head) * look;
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
	computeCamera(eye, lookAt, up);
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
