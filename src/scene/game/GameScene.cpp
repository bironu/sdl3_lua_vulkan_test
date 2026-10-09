#include "app/Application.h"
#include "scene/game/GameScene.h"
#include "geo/Calculator.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "sdl/SDLGamepad.h"
#include "resources/Resources.h"
#include "sdl/SDLVulkanWindow.h"
#include "ui/PadNames.h"
#include "scene/character/CharacterList.h"
#include "scene/characterselect/CharacterSelectScene.h"
#include "scene/common/ModelFactory.h"
#include "scene/common/SceneWindow.h"
#include "scene/common/ScreenCoords.h"
#include "scene/game/GameSession.h"
#include "scene/SceneHost.h"
#include "scene/SceneRegistry.h"
#include "vk/VulkanMath.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace game
{

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

// フィールドの範囲内に収める。下限が上限を超えないよう max を使う
float clampInside(float v, float r, float size)
{
	return std::clamp(v, r, std::max(r, size - r));
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

// なめらかに始まり、なめらかに終わる補間関数(t=0〜1)
float smoothstep01(float t)
{
	return t * t * (3.0f - 2.0f * t);
}

// モーションの名前(res/lua/game.assets.lua の motions の表のキー。GameScene::Motion の並びと同じ)
const std::vector<std::string> kMotionNames = {
	"idle", "walk", "slowRun", "fastRun", "climb", "roll", "punchRight", "punchLeft", "kickHigh", "roundhouse",
};
}

GameScene::GameScene() = default;

GameScene::~GameScene() = default;

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
		climbWeight_ = climbSlope_ = tilt_ = 0.0f; // 登りの混ぜ・傾きを残さない
		cameraArm_ = 1.0f;
		pendingAuto_ = -1;
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

void GameScene::forwardKey(const char *name, bool down)
{
	if(paused_ && pauseMenu_){
		pauseMenu_->onKey(name, down);
	}
	else if(hud_){
		hud_->onKey(name, down);
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
			forwardKey(SDL_GetKeyName(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
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
		// PadNavigator が DPAD を扱うので除外する
		if(*name && button != SDL_GAMEPAD_BUTTON_DPAD_UP && button != SDL_GAMEPAD_BUTTON_DPAD_DOWN
			&& button != SDL_GAMEPAD_BUTTON_DPAD_LEFT && button != SDL_GAMEPAD_BUTTON_DPAD_RIGHT){
			forwardKey(name, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
		}
		break;
	}
	case SDL_EVENT_MOUSE_MOTION: {
		if(paused_){
			const auto [x, y] = windowToScreen(getWindow(), *uiContext_, event.motion.x, event.motion.y);
			pauseMenu_->onMouseMove(x, y);
			break;
		}
		// 視点の回転(マウスはウィンドウに取り込み済み): 右へ動かすと右を向く、下へ動かすと見下ろす
		const float radiansPerPixel = settings_.camera.mouseSpeed * sensitivity_;
		cameraYaw_ -= event.motion.xrel * radiansPerPixel;
		cameraPitch_ = std::clamp(cameraPitch_ + event.motion.yrel * radiansPerPixel, kMinPitch, kMaxPitch);
		break;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if(paused_){
			const auto [x, y] = windowToScreen(getWindow(), *uiContext_, event.button.x, event.button.y);
			pauseMenu_->onMouseButton(event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
		}
		break;
	default:
		break;
	}
}

void GameScene::setupLighting(SDL_::VulkanWindow &window)
{
	// ライティング: 軽さを優先して、シャドウマップは使わず、足元の丸い影にする。点光源は無し
	window.setShadowMapsEnabled(false);
	window.clearPointLights();
	window.setAmbientEnvironment(true, geo::Vector3f(0.36f, 0.32f, 0.27f), 0.0f, 0.5f, 0.6f);
	window.setLight(geo::Vector3f(-0.4f, -1.0f, -0.6f), 0.7f, 0.7f, 0.7f, 0.4f);
	blob_ = std::make_unique<BlobShadow>(window);
}

void GameScene::loadField(SDL_::VulkanWindow &window)
{
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
	// 動作確認用: VULKAN_START=x,z で、開始位置を指定する(最初の位置へ戻るときも、そこへ戻る)
	if(const auto &start = automation_.startPosition()){
		startX_ = playerX_ = start->first;
		startZ_ = playerZ_ = start->second;
	}
}

// プレイヤーのモデルを用意する。作れなかったらfalse
bool GameScene::loadPlayer(SDL_::VulkanWindow &window)
{
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
		return false;
	}
	{
		float top = 0.0f;
		for(const auto &vertex : player_->data().vertices){
			top = std::max(top, vertex.position[1]);
		}
		applyModelHeight(top);
	}
	player_->setAmbientBoost(0.0f);
	return true;
}

void GameScene::loadEnemies(SDL_::VulkanWindow &window)
{
	enemies_.reset(); // 前の敵のGPUの資源を先に手放す(描画中のフレームが使う分は、描画の側が持っている)
	enemies_ = std::make_unique<EnemyHorde>(window, resources(), loadEnemyTypes(), map_, playerX_, playerZ_);
}

void GameScene::loadMotions()
{
	if(player_->skeleton()){
		assert(kMotionNames.size() == MotionCount); // 列挙と名前の並びが食い違っていないか
		const auto paths = loadMotionPaths(kAssetManifest, kMotionNames);
		for(int i = 0; i < MotionCount; ++i){
			if(paths[i].empty()){
				continue;
			}
			if(const auto animation = resources().animation(paths[i])){
				if(i == MotionRoll){
					rollAnimation_ = animation;
					buildRollProfile();
				}
				motions_[i] = model::VrmaPlayer::create(animation, player_->data(), *player_->skeleton(), player_->morphs());
				if(motions_[i]){
					motions_[i]->setInPlace(true); // 前へ進むのはキャラの位置(playerX_/Z_)で行う。モーションは、その場の動きだけ
				}
			}
		}
	}
}

void GameScene::measureLoopSeams()
{
	// ループのつなぎ目: 最初と最後の姿勢が大きく違う(ループ用に作られていない)モーションを調べる(loopSeam_: 骨の回転の差の合計。ラジアン)。
	// 差が大きいと、ループの折り返しで姿勢が飛ぶ(登りのモーションが、4秒ごとに唐突に立ち上がって見える原因だった)。そのモーションは、折り返しで混ぜてつなぐ(applyLooped)
	if(auto *skeleton = player_->skeleton()){
		for(int m = 0; m < MotionCount; ++m){
			if(!motions_[m]){
				continue;
			}
			Pose first, last;
			skeleton->resetPose();
			motions_[m]->apply(*skeleton, nullptr, 0.0f);
			capturePose(*skeleton, first);
			skeleton->resetPose();
			motions_[m]->apply(*skeleton, nullptr, motions_[m]->duration());
			capturePose(*skeleton, last);
			float sum = 0.0f;
			for(size_t i = 0; i < first.rotations.size(); ++i){
				const auto &a = first.rotations[i], &b = last.rotations[i];
				const float d = std::min(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w));
				sum += 2.0f * std::acos(d);
			}
			loopSeam_[m] = sum;
		}
		skeleton->resetPose();
	}
}

void GameScene::setupUi(SDL_::VulkanWindow &window)
{
	// HUD: Luaのウィジェット。スクリプトの読み込み一覧(hud.assets.lua)があれば、先に読む
	resources().loadManifest(ResourcePaths::assetsManifestFor(kHudScript));
	uiContext_ = std::make_unique<ui::UiContext>(window, getResources(), resources());
	// HUD とポーズ画面で共通のコールバック
	ui::UiScript::Callbacks baseCallbacks;
	baseCallbacks.quit = [this]{ quit(); };
	baseCallbacks.command = [this](const std::string &name, double value){ command(name, value); };
	hud_ = std::make_unique<ui::UiScript>(*uiContext_, baseCallbacks);
	hud_->load(kHudScript);
	// ポーズ画面(開くときにload): 共通のコールバックにシーン遷移機能を追加
	ui::UiScript::Callbacks pauseCallbacks = baseCallbacks;
	pauseCallbacks.changeScene = [this](const std::string &name){
		if(auto next = SceneRegistry::create(name)){
			getHost().registerNextScene(std::move(next));
			finish();
		}
	};
	pauseMenu_ = std::make_unique<ui::UiScript>(*uiContext_, std::move(pauseCallbacks));
}

void GameScene::onCreate(uint32_t tick)
{
	Scene::onCreate(tick);
	settings_ = loadGameSettings(); // 速さ・カメラ・クロスフェードなどの調整値(res/lua/data/game_settings.lua。F5で読み直す)
	automation_ = DebugAutomation::fromEnv();
	auto &window = vulkanWindow(*this);
	auto &res = getResources();
	window.setScreenSize(static_cast<float>(res.getScreenWidth()), static_cast<float>(res.getScreenHeight()));
	window.setClearColor(0.55f, 0.7f, 0.9f);

	setupLighting(window);
	loadField(window);
	if(!loadPlayer(window)){
		quit();
		return;
	}
	loadMotions();
	measureLoopSeams();
	playerY_ = map_.heightAt(playerX_, playerZ_);
	playerTransform_.setPos(geo::Vector3f(playerX_, playerY_, playerZ_));
	playerTransform_.setScale(geo::Vector3f(1.0f, 1.0f, 1.0f));
	loadEnemies(window);
	setupUi(window);

	// 動作確認用: VULKAN_PITCH=ラジアン で、カメラの縦の角度の初期値(例: -0.6 で下から見上げる、-1.57 で真上)
	if(const auto &pitch = automation_.pitch()){
		cameraPitch_ = std::clamp(*pitch, kMinPitch, kMaxPitch);
	}
	// 動作確認用: VULKAN_YAW=ラジアン で、カメラの水平の角度の初期値(例: 1.57 で横から)
	if(const auto &yaw = automation_.yaw()){
		cameraYaw_ = *yaw;
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

// 転がるモーションは、変換のとき、前へ進む分(rollMotionTravel)を、時間に比例して引いてある(その場で転がる形)。
// 引く前の腰の前後の動き = いまの腰の前後の動き + 引いた分(rollMotionTravel × 時刻の割合)。これが、モーションの足の動きに合った進み方。
// 常に前へ(戻らない)になるよう、累積の最大を取って、最後を1にそろえる
void GameScene::buildRollProfile()
{
	rollProfile_.clear();
	const auto &animation = rollAnimation_;
	if(!animation || animation->hipsTimes.size() < 2 || animation->hipsTimes.size() != animation->hipsTranslations.size()){
		return;
	}
	const auto &times = animation->hipsTimes;
	const auto &positions = animation->hipsTranslations;
	const float duration = times.back();
	const float travel = std::max(settings_.player.rollMotionTravel, 0.01f);
	constexpr int kSamples = 128;
	size_t k = 0;
	float peak = 0.0f;
	for(int i = 0; i <= kSamples; ++i){
		const float t = duration * static_cast<float>(i) / kSamples;
		while(k + 2 < times.size() && times[k + 1] < t){
			++k;
		}
		const float span = std::max(times[k + 1] - times[k], 1e-6f);
		const float f = std::clamp((t - times[k]) / span, 0.0f, 1.0f);
		const float z = positions[k].z + (positions[k + 1].z - positions[k].z) * f - positions[0].z;
		peak = std::max(peak, z + travel * t / std::max(duration, 1e-6f));
		rollProfile_.push_back(peak);
	}
	const float total = std::max(rollProfile_.back(), 1e-4f);
	for(auto &value : rollProfile_){
		value = std::clamp(value / total, 0.0f, 1.0f);
	}
}

float GameScene::rollProgress(float normalizedTime) const
{
	const float t = std::clamp(normalizedTime, 0.0f, 1.0f);
	if(rollProfile_.size() < 2){
		return t; // 表が無いときは、等速
	}
	const float x = t * static_cast<float>(rollProfile_.size() - 1);
	const size_t i = std::min(static_cast<size_t>(x), rollProfile_.size() - 2);
	return rollProfile_[i] + (rollProfile_[i + 1] - rollProfile_[i]) * (x - static_cast<float>(i));
}

float GameScene::loopOverlap(int motion) const
{
	const auto &player = motions_[motion];
	if(!player || loopSeam_[motion] <= settings_.motion.seamThreshold){
		return 0.0f;
	}
	return std::min(settings_.motion.loopBlend, player->duration() * 0.4f);
}

void GameScene::blendPose(model::Skeleton &skeleton, const Pose &from, float alpha)
{
	const size_t limit = std::min(skeleton.boneCount(), from.rotations.size());
	for(size_t i = 0; i < limit; ++i){
		const int bone = static_cast<int>(i);
		const model::Vec3 &fromPos = from.translations[i];
		const model::Vec3 &toPos = skeleton.boneTranslation(bone);
		skeleton.setBoneRotation(bone, model::Quat::slerp(from.rotations[i], skeleton.boneRotation(bone), alpha));
		skeleton.setBoneTranslation(bone, {fromPos.x + (toPos.x - fromPos.x) * alpha, fromPos.y + (toPos.y - fromPos.y) * alpha, fromPos.z + (toPos.z - fromPos.z) * alpha});
	}
}

float GameScene::loopLength(int motion) const
{
	const auto &player = motions_[motion];
	// fmod の除数が 0 になるのを防ぐ
	return player ? player->duration() - loopOverlap(motion) : 1.0f;
}

// つなぎ目を混ぜるループ: 周期 L = 長さ - F。時刻 u が 0〜F の間は、(終わり側 L+u の姿勢)から(始め側 u の姿勢)へ混ぜる(u=0 で前の周期の終わりと、u=F で始めの続きと、ちょうどつながる)
void GameScene::applyLooped(int motion, float time, model::Skeleton &skeleton, model::MorphSet *morphs)
{
	const auto &player = motions_[motion];
	if(!player){
		return;
	}
	const float duration = player->duration();
	const float overlap = loopOverlap(motion);
	if(overlap == 0.0f){
		player->apply(skeleton, morphs, std::min(time, duration));
		return;
	}
	const float length = duration - overlap;
	const float u = std::fmod(time, length);
	if(u >= overlap){
		player->apply(skeleton, morphs, u);
		return;
	}
	player->apply(skeleton, morphs, length + u);
	capturePose(skeleton, loopTail_);
	skeleton.resetPose();
	player->apply(skeleton, morphs, u);
	const float t = u / overlap;
	const float alpha = smoothstep01(t);
	blendPose(skeleton, loopTail_, alpha);
}

bool GameScene::stepMove(float dirX, float dirZ, float distance, float maxSlope)
{
	const float beforeX = playerX_, beforeZ = playerZ_;
	// フィールドの縁: 外へは出られない(壁に沿っては滑れる。軸ごとに止める)
	const float targetX = clampInside(playerX_ + dirX * distance, settings_.player.radius, fieldWidth());
	const float targetZ = clampInside(playerZ_ + dirZ * distance, settings_.player.radius, fieldDepth());
	// 地面: 歩けないタイル(水)と急な勾配へは進めない(沿って滑れる)
	field::moveOnField(map_, movementRules_, settings_.player.radius, playerX_, playerZ_, targetX, targetZ, maxSlope);
	// 置物: めり込んだら、外へ押し出す(壁に沿って滑れる)。押し出しでフィールドの外・水・急な所へ出たら、押し出す前へ戻す
	float pushedX = playerX_, pushedZ = playerZ_;
	if(propCollision_.resolve(pushedX, pushedZ, settings_.player.radius)){
		pushedX = clampInside(pushedX, settings_.player.radius, fieldWidth());
		pushedZ = clampInside(pushedZ, settings_.player.radius, fieldDepth());
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
	if(motion == MotionRoll){
		motionTime_ = std::min(settings_.player.rollStartOffset, motions_[motion]->duration() * 0.5f); // 助走・かがみを飛ばして、すぐ飛び込む
	}
}

// 入力を読む: キーボード・ゲームパッド(視点の回転もここ)・動作確認用の上書き。シーン開始時に押されていたボタンの無視まで行う
GameScene::PlayerInput GameScene::gatherInput(float dt, uint32_t tick)
{
	PlayerInput input;
	const bool *keys = Application::getKeybordState();
	input.right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
	input.forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
	input.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
	input.actionDown = keys[SDL_SCANCODE_SPACE];
	input.attack[0] = keys[SDL_SCANCODE_X] != 0; // R1 R2 L1 L2
	input.attack[1] = keys[SDL_SCANCODE_V] != 0;
	input.attack[2] = keys[SDL_SCANCODE_Z] != 0;
	input.attack[3] = keys[SDL_SCANCODE_C] != 0;
	// ゲームパッド: 左スティックで移動(傾きの分だけ進む)、右スティックでカメラ(視点)を回す
	if(const auto pad = getResources().getGamepad()){
		float lx, ly, rx, ry;
		pad->leftStick(lx, ly);
		pad->rightStick(rx, ry);
		// 歩き出し・走り出しに遊びを持たせる(スティックが最大まで倒れ切らない・値がぶれる、への対策):
		//   歩き: moveEnter 以上で動き出し、moveExit を下回るまで動き続ける(小さい傾きは無視)
		//   走り: runStick 以上で走り出し、runExit を下回って runGrace 秒たつまで走り続ける
		const float stickLength = std::sqrt(lx * lx + ly * ly);
		stickMoving_ = stickMoving_ ? stickLength >= settings_.input.moveExit : stickLength >= settings_.input.moveEnter;
		if(stickMoving_){
			input.right += lx;
			input.forward -= ly; // スティックは下が+
		}
		if(stickRunning_){
			stickDip_ = stickLength >= settings_.input.runExit ? 0.0f : stickDip_ + dt;
			stickRunning_ = stickDip_ < settings_.input.runGrace;
		}
		else{
			stickRunning_ = stickLength >= settings_.input.runStick;
			stickDip_ = 0.0f;
		}
		input.run = input.run || stickRunning_;
		cameraYaw_ -= rx * settings_.camera.yawSpeed * sensitivity_ * dt;
		cameraPitch_ = std::clamp(cameraPitch_ + ry * settings_.camera.pitchSpeed * sensitivity_ * dt, kMinPitch, kMaxPitch);
		input.actionDown = input.actionDown || pad->button(SDL_GAMEPAD_BUTTON_SOUTH);
		input.attack[0] = input.attack[0] || pad->button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
		input.attack[1] = input.attack[1] || pad->axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > settings_.input.triggerOn;
		input.attack[2] = input.attack[2] || pad->button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		input.attack[3] = input.attack[3] || pad->axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > settings_.input.triggerOn;
	}
	// 動作確認用の環境変数(DebugAutomation参照): 入力の上書き(AUTOWALK/AUTORUN)と、アクションのボタン(AUTOACTION)
	automation_.overrideMove(input.forward, input.right);
	automation_.overrideRun(input.run);
	while(const std::string *name = automation_.nextAction(static_cast<int>(tick - startTick_))){
		pendingAuto_ = *name == "roll" ? MotionRoll : *name == "pr" ? MotionPunchRight : *name == "pl" ? MotionPunchLeft : *name == "kh" ? MotionKickHigh : MotionRoundhouse;
	}
	// シーンが始まったとき(キャラクタ選択のAボタンなど)に押されていたボタンは、いったん離されるまで無いものとして扱う
	// (押したままゲームが始まると、離したときに「単押し」とみなされて、転がってしまうため)
	if(!inputArmed_){
		if(input.actionDown || input.attack[0] || input.attack[1] || input.attack[2] || input.attack[3]){
			input.actionDown = input.attack[0] = input.attack[1] = input.attack[2] = input.attack[3] = false;
		}
		else{
			inputArmed_ = true;
		}
	}
	if(automation_.holdAction()){
		input.actionDown = true; // 動作確認用: Aボタンを押しっぱなし
	}
	return input;
}

// 入力を、カメラから見た水平の移動の向きへ直す
GameScene::MoveDir GameScene::moveDirection(const PlayerInput &input) const
{
	// カメラの前(水平)と右(水平)。カメラは注視点の(sin yaw, cos yaw)側にいる
	const float fx = -std::sin(cameraYaw_), fz = -std::cos(cameraYaw_);
	const float rx = std::cos(cameraYaw_), rz = -std::sin(cameraYaw_);
	MoveDir dir;
	dir.x = fx * input.forward + rx * input.right;
	dir.z = fz * input.forward + rz * input.right;
	dir.length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
	dir.magnitude = std::min(dir.length, 1.0f); // スティックを少しだけ倒したときは、ゆっくり進む(キーボードは常に1)
	if(dir.length > 0.0f){
		dir.x /= dir.length;
		dir.z /= dir.length;
	}
	return dir;
}

// ボタンの押し始め・離したとき(アクションの開始)
void GameScene::handleActions(float dt, const PlayerInput &input, const MoveDir &dir)
{
	const bool busy = isAction(motion_);
	if(input.actionDown){
		actionHeldTime_ += dt;
	}
	if(settings_.input.rollOnPress){
		if(input.actionDown && !actionHeld_ && !busy){
			startAction(MotionRoll, dir.x, dir.z); // 押した瞬間に転がり始める
		}
		if(!input.actionDown){
			actionHeldTime_ = 0.0f;
		}
	}
	else if(actionHeld_ && !input.actionDown){
		if(actionHeldTime_ < settings_.input.tapTime && !busy){
			startAction(MotionRoll, dir.x, dir.z); // 単押し(離したときに転がる)
		}
		actionHeldTime_ = 0.0f;
	}
	actionHeld_ = input.actionDown;
	static const int kAttackMotions[kAttackCount] = {MotionPunchRight, MotionKickHigh, MotionPunchLeft, MotionRoundhouse};
	for(int i = 0; i < kAttackCount; ++i){
		if(input.attack[i] && !actionPrev_[i] && !isAction(motion_)){
			startAction(kAttackMotions[i], 0.0f, 0.0f);
		}
		actionPrev_[i] = input.attack[i];
	}
	if(pendingAuto_ >= 0){
		if(!isAction(motion_)){
			startAction(pendingAuto_, dir.x, dir.z);
		}
		pendingAuto_ = -1;
	}
}

// 動き: アクション中は、転がるときだけ前へ進む(坂の登り始め・終わりのモーションの間は、止まる)。それ以外は、入力の向きへ
void GameScene::updateLocomotion(float dt, const PlayerInput &input, const MoveDir &dir)
{
	const float dx = dir.x, dz = dir.z, length = dir.length;
	walking_ = false;
	if(isAction(motion_)){
		const auto &player = motions_[motion_];
		const float rate = motion_ == MotionRoll ? settings_.player.rollRate : 1.0f;
		const float before = motionTime_ / player->duration();
		motionTime_ += dt * rate;
		if(motion_ == MotionRoll){
			// 前へ進むのは、モーションの足の動きに合わせて(足が着いて立ち上がる間は進まない)。飛ばした頭の分は、進む距離に含めない(残りで、全部の距離を進む)
			const float after = std::min(motionTime_ / player->duration(), 1.0f);
			const float afterProgress = rollProgress(after);
			const float skipped = rollProgress(std::min(settings_.player.rollStartOffset / player->duration(), 0.5f));
			const float scale = 1.0f / std::max(1.0f - skipped, 1e-3f);
			stepMove(rollDirX_, rollDirZ_, settings_.player.rollDistance * scale * (afterProgress - rollProgress(before)));
			// 転がり終わって立ち上がる間に、スティックを倒していたら、立ち上がりを切り上げて、そのまま歩き・走りへつなぐ
			if(length > 0.0f && afterProgress >= settings_.player.rollCancelProgress){
				motionTime_ = player->duration();
			}
		}
		if(motionTime_ >= player->duration()){
			motion_ = MotionIdle; // 終わり。下で、立ち・歩き・走り・登りへ戻る
			motionTime_ = 0.0f;
		}
	}
	if(!isAction(motion_)){
		int wanted = MotionIdle;
		float speed = 0.0f;
		const float climbLimit = movementRules_.maxClimbSlope * 1.05f;
		// 坂登り: 切り替えではなく、坂の勾配に応じて、登りのモーションを連続的に混ぜる(climbWeight_: 0=歩き・走りだけ、1=登りだけ)。
		// 勾配が blendLow 以下なら歩き・走り、blendHigh 以上なら登りで、その間はなめらかに混ぜる。登りへは素早く、歩き・走りへはゆっくり戻る。
		// 勾配は、すぐ前(0.15m)と少し先(0.5m)の、急な方(登り坂だけ)
		const float near = length > 0.0f ? field::slopeAhead(map_, playerX_, playerZ_, dx, dz, 0.15f) : 0.0f;
		const float ahead = length > 0.0f ? field::slopeAhead(map_, playerX_, playerZ_, dx, dz) : 0.0f;
		const float slope = std::max({near, ahead, 0.0f});
		float target = 0.0f;
		if(motions_[MotionClimb] && length > 0.0f){
			const float span = std::max(settings_.climb.blendHigh - settings_.climb.blendLow, 1e-3f);
			const float t = std::clamp((slope - settings_.climb.blendLow) / span, 0.0f, 1.0f);
			target = smoothstep01(t);
		}
		const float rate = target > climbWeight_ ? settings_.climb.riseRate : settings_.climb.fallRate;
		climbWeight_ += (target - climbWeight_) * std::min(1.0f, rate * dt);
		if(length > 0.0f){
			climbSlope_ += (slope - climbSlope_) * std::min(1.0f, kClimbSlopeSmoothRate * dt);
			walking_ = true;
			const bool fast = input.run && input.actionDown && actionHeldTime_ >= settings_.input.tapTime;
			if(input.run){
				wanted = fast ? MotionFastRun : MotionSlowRun;
				speed = fast ? settings_.player.fastRunSpeed : settings_.player.slowRunSpeed;
			}
			else{
				wanted = MotionWalk;
				speed = settings_.player.walkSpeed * dir.magnitude;
			}
			if(!motions_[wanted]){ // 走りのモーションが読めなかったときは、歩きで
				wanted = MotionWalk;
			}
			// 速さも、登りの混ざり具合に合わせて、歩き・走りの速さから登りの速さへ。急な坂へは、登りの上限(maxClimbSlope)まで進める
			speed = speed + (settings_.player.climbSpeed - speed) * climbWeight_;
			stepMove(dx, dz, speed * dt, motions_[MotionClimb] ? climbLimit : -1.0f);
			heading_ = approachAngle(heading_, std::atan2(dx, dz), settings_.player.turnSpeed * dt);
		}
		// モーションの切り替え: ループの途中の位置(割合)を引き継ぐ(足の運びが飛ばないように)
		if(wanted != motion_ && motions_[wanted] && motions_[motion_]){
			motionTime_ = motionTime_ / motions_[motion_]->duration() * motions_[wanted]->duration();
		}
		motion_ = wanted;
	}
	else{
		climbWeight_ += (0.0f - climbWeight_) * std::min(1.0f, kClimbSlopeSmoothRate * dt); // アクション中は、登りの混ざりを戻す
	}
}

// 坂を登っている(登りが混ざっている)とき、キャラを坂に沿って(坂の角度の tiltFactor の割合だけ)後ろへ傾ける。なめらかに追いつく
void GameScene::updateTilt(float dt)
{
	const float tiltTarget = std::atan(climbSlope_) * settings_.climb.tiltFactor * climbWeight_;
	tilt_ += (tiltTarget - tilt_) * std::min(1.0f, settings_.climb.tiltSmooth * dt);
}

// モーションを当てる: ループ、登りの混ぜ、クロスフェード
void GameScene::applyMotion(float dt)
{
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
			motionTime_ = std::fmod(motionTime_ + dt, loopLength(motion_) + 1e-3f); // 立ち・歩き・走りは、ループ
			applyLooped(motion_, motionTime_, *skeleton, morphs);
		}
		else{
			player->apply(*skeleton, morphs, std::min(motionTime_, player->duration()));
		}
	}
	// 坂登りの混ぜ: 歩き・走り(いま当てた姿勢)と、登りのモーションの姿勢を、climbWeight_ で混ぜる
	if(!isAction(motion_) && climbWeight_ > 0.002f && motions_[MotionClimb]){
		climbTime_ = std::fmod(climbTime_ + dt, loopLength(MotionClimb) + 1e-3f);
		capturePose(*skeleton, basePose_);
		applyLooped(MotionClimb, climbTime_, *skeleton, morphs);
		blendPose(*skeleton, basePose_, climbWeight_);
	}
	// クロスフェード: モーションが切り替わった瞬間に、直前の姿勢を覚えて、新しい姿勢へ混ぜていく
	if(appliedMotion_ >= 0 && appliedMotion_ != motion_ && !lastPose_.rotations.empty()){
		fadeFrom_ = lastPose_;
		fadeTime_ = 0.0f;
		fadeDuration_ = isAction(motion_) ? settings_.fade.toAction : isAction(appliedMotion_) ? (walking_ ? settings_.fade.fromActionMoving : settings_.fade.fromAction) : settings_.fade.locomotion;
	}
	appliedMotion_ = motion_;
	if(fadeTime_ < fadeDuration_ && fadeFrom_.rotations.size() == skeleton->boneCount()){
		fadeTime_ += dt;
		const float t = std::clamp(fadeTime_ / fadeDuration_, 0.0f, 1.0f);
		const float alpha = smoothstep01(t);
		blendPose(*skeleton, fadeFrom_, alpha);
	}
	capturePose(*skeleton, lastPose_);
	player_->updatePose(dt);
}

// 入力に合わせてプレイヤーを動かし、モーションを当てる:
//   左スティックを倒しきらない: 歩き。最大(キーボードはShift): Slow Run。Aボタン(キーボードはSpace)を押しっぱなしで最大: Fast Run
//   歩いて登れない少し急な坂(maxSlope〜maxClimbSlope)へ進む: Climbing Slope(ゆっくり登る)
//   Aボタンの単押し: Stand To Roll。R1: Punching Right、R2: Mma Kick Right High、L1: Punching Left、L2: Roundhouse Kick(キーボードは X V Z C)
//   アクションは、終わるまで他の操作を受けない(転がるときだけ、前へ進む)
void GameScene::updatePlayer(float dt, uint32_t tick)
{
	const PlayerInput input = gatherInput(dt, tick);
	const MoveDir dir = moveDirection(input);
	handleActions(dt, input, dir);
	updateLocomotion(dt, input, dir);
	updateTilt(dt);
	playerY_ = map_.heightAt(playerX_, playerZ_);
	playerTransform_.setPos(geo::Vector3f(playerX_, playerY_, playerZ_));
	playerTransform_.setRotation(geo::Quaternionf::createRotater(heading_, geo::Vector3f(0.0f, 1.0f, 0.0f)) * geo::Quaternionf::createRotater(-tilt_, geo::Vector3f(1.0f, 0.0f, 0.0f)));
	applyMotion(dt);
}

void GameScene::drawScene(const geo::Matrix4x4f &viewProj, const geo::Vector3f &eye)
{
	auto &window = vulkanWindow(*this);
	fieldRenderer_->draw(viewProj);
	propRenderer_->draw(map_, viewProj);
	// プレイヤーと、足元の丸い影
	window.draw(player_, viewProj, playerTransform_.getMatrix());
	blob_->draw(window, viewProj, playerX_, playerY_, playerZ_, settings_.player.shadowRadius, settings_.player.shadowOpacity);
	enemies_->draw(window, viewProj, *blob_, eye);
}

// F5の読み直し: データ定義(文字列・フォントなど)、HUDのスクリプト、調整値・地形の設定。
// カメラの距離は調整値(game_settings.lua の camera.distance)で決まるので、読み直すと、その値になる
void GameScene::reloadAll()
{
	SDL_Log("hud: reloading %s", kHudScript);
	getResources().reload();
	hud_->load(kHudScript);
	reloadSettings();
	loadEnemies(vulkanWindow(*this));
}

// HUDへ、ゲームの状態を渡して、スクリプトのupdateを進める(描画はdrawHudで)
void GameScene::updateHud(float dt, uint32_t tick)
{
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
	buildRollProfile(); // 転がる進み方(rollMotionTravel を変えたとき)
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
		const float theta = rise * kHalfPi;
		const float startDistance = std::cos(groundPitch) * cameraDistance_;
		const float bodyDistance = std::min(settings_.camera.bodyDistance * modelHeight_ / settings_.camera.referenceHeight, startDistance);
		const float easedApproach = smoothstep01(approach);
		const float horizontal = (startDistance + (bodyDistance - startDistance) * easedApproach) * std::cos(theta);
		const float height = playerY_ + minEyeHeight_ + (headTop_ - minEyeHeight_) * std::sin(theta);
		eye = geo::Vector3f(playerX_ + sinYaw * horizontal, height, playerZ_ + cosYaw * horizontal);
		const float look = rise * rise; // 見る先が頭から離れるのは、上がる後半の終盤から(それまでは、体が視界に残る)
		lookAt = head + (eye + geo::Vector3f(0.0f, kLookUpOffset, 0.0f) - head) * look;
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
	// 動作確認用の環境変数(DebugAutomation参照): ポーズ(AUTOPAUSE)・読み直し(AUTORELOAD)・ポーズ中のメニューへのマウス(AUTOMOUSE)
	const int elapsedMs = static_cast<int>(tick - startTick_);
	if(!paused_ && automation_.takePause(elapsedMs)){
		setPaused(true);
	}
	if(automation_.takeReload(elapsedMs)){
		reloadHud_ = true;
	}
	if(DebugAutomation::Mouse mouse; paused_ && automation_.takeMouse(elapsedMs, mouse)){
		DebugAutomation::sendMouse(*pauseMenu_, mouse);
	}
	if(!paused_){
		updatePlayer(dt, tick);
		enemies_->update(dt, map_, movementRules_, propCollision_, playerX_, playerZ_);
	}

	// 三人称のカメラ
	geo::Vector3f eye, lookAt, up;
	computeCamera(dt, eye, lookAt, up);
	const auto view = geo::createLookAt<float>(eye, lookAt, up);
	const auto proj = vk_::createPerspective(settings_.camera.fov * (kPi / 180.0f), window.getScreenWidth(), window.getScreenHeight(), settings_.camera.nearPlane, settings_.camera.farPlane);
	window.setCameraPosition(eye);

	drawScene(proj * view, eye);
	if(reloadHud_){
		reloadHud_ = false;
		reloadAll();
	}
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
