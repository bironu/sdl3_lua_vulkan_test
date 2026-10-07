#include "scene/VulkanSampleScene.h"
#include <algorithm>
#include "resources/Resources.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include "geo/Calculator.h"
#include "vk/VulkanMath.h"
#include "vk/PrimitiveMeshes.h"
#include <SDL3/SDL_events.h>
#include <cmath>
#include <future>
#include <thread>
#include <map>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
// VulkanSampleSceneは必ずVulkanWindowに属する
SDL_::VulkanWindow &vulkanWindow(Scene &scene)
{
	return static_cast<SDL_::VulkanWindow&>(scene.getWindow());
}

// 一辺1の立方体(面ごとに頂点を持つ24頂点。面ごとに色が違い、UVは面全体に0〜1)
// 白/灰の市松模様。cellは1マスのピクセル数
std::shared_ptr<SDL_::Image> createChecker(int size, int cell)
{
	auto checker = std::make_shared<SDL_::Image>(size, size);
	checker->fillRect(SDL_::Color(90, 90, 90, 255));
	for(int y = 0; y < size; y += cell){
		for(int x = 0; x < size; x += cell){
			if(((x / cell) + (y / cell)) % 2 == 0){
				checker->fillRect(Rect(x, y, cell, cell), SDL_::Color(255, 255, 255, 255));
			}
		}
	}
	return checker;
}

}

VulkanSampleScene::VulkanSampleScene() = default;
VulkanSampleScene::~VulkanSampleScene() = default;

void VulkanSampleScene::dispatch(const SDL_Event &event)
{
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		if (event.key.key == SDLK_ESCAPE) {
			quit();
		}
		else if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9 && !event.key.repeat) {
			const int index = static_cast<int>(event.key.key - SDLK_0);
			if (index == 0 && activeMotion_ == 0) {
				activeMotion_ = -1; // 0でダンスの再生/停止
				lastVrmTick_ = 0;   // 姿勢が飛ぶので、揺れ物(スプリングボーン)を合わせ直す
			}
			else {
				startMotion(index);
			}
		}
		else if (event.key.key == SDLK_R && !event.key.repeat) {
			// Rで、カメラの位置(注視点)・向き・拡大を初期値に戻す
			cameraYaw_ = kInitialCameraYaw;
			cameraPitch_ = allMode_ ? kAllCameraPitch : kInitialCameraPitch;
			cameraDistance_ = allMode_ ? kAllCameraDistance : kInitialCameraDistance;
			for (int i = 0; i < 3; ++i) {
				cameraTarget_[i] = kInitialCameraTarget[i];
			}
		}
		else if (event.key.key == SDLK_V && !event.key.repeat) {
			// Vキーで、res/model/のVRMファイルを順に切り替える(MMDのモデルがあれば、最後にMMD。その次は最初のVRMへ戻る)
			const int slots = static_cast<int>(vrmPaths_.size()) + (aquaModel_ ? 1 : 0);
			if (slots > 0) {
				const int current = showVrm_ ? vrmIndex_ : static_cast<int>(vrmPaths_.size());
				pendingVrmSlot_ = (current + 1) % slots;
			}
		}
		else if (event.key.key == SDLK_B && !event.key.repeat) {
			// Bで、アンチエイリアスを None → MSAA 4x → FXAA → MSAA 4x + FXAA の順に切り替える
			vulkanWindow(*this).cycleAntiAliasing();
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_C && !event.key.repeat) {
			// Cで、フラスタムカリングの有効/無効を切り替える(負荷の比較用)
			auto &window = vulkanWindow(*this);
			window.setFrustumCulling(!window.frustumCulling());
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_SPACE && !event.key.repeat) {
			// Spaceで、アニメーションの一時停止/再開(カメラは動かせる)
			const uint32_t now = SDL_GetTicks();
			if (paused_) {
				pausedTotal_ += now - pausedAt_;
			}
			else {
				pausedAt_ = now;
			}
			paused_ = !paused_;
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_M && !event.key.repeat) {
			// Mで、影をシャドウマップ(形のある影・重い)と丸い影(軽い)で切り替える
			auto &window = vulkanWindow(*this);
			window.setShadowMapsEnabled(!window.shadowMapsEnabled());
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_H && !event.key.repeat) {
			cameraLight_ = !cameraLight_; // Hで、光をカメラ追従/ワールド固定に切り替える
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_L && !event.key.repeat) {
			// Lで、点光源(目印のランプ付き)の数を 0 → 1 → 2 → 3 → 0 と切り替える(影の負荷は1灯あたり約3ms)
			activeLamps_ = (activeLamps_ + 1) % (kLampCount + 1);
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_P && !event.key.repeat) {
			// Pで、点光源の影の更新間隔を 1 → 2 → 4 フレーム(枠の使用回数)に切り替える(負荷の比較用)
			auto &window = vulkanWindow(*this);
			window.setPointShadowInterval(window.pointShadowInterval() >= 4 ? 1 : window.pointShadowInterval() * 2);
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_G && !event.key.repeat) {
			// Gで、res/model/の全モデルを並べて表示するか、単独表示(Vで切り替え)かを切り替える
			allMode_ = !allMode_;
			cameraDistance_ = allMode_ ? kAllCameraDistance : kInitialCameraDistance;
			cameraPitch_ = allMode_ ? kAllCameraPitch : kInitialCameraPitch;
			updateWindowTitle();
		}
		else if (event.key.key == SDLK_TAB && !event.key.repeat) {
			toonShading_ = !toonShading_; // TABでトゥーンシェーディングと通常のシェーディングを切り替える
		}
		break;
	case SDL_EVENT_MOUSE_MOTION:
		// 左ボタンのドラッグで、キャラを中心にカメラを回す(右へドラッグ=シーンが右へ回る)
		if (event.motion.state & SDL_BUTTON_LMASK) {
			constexpr float kRadiansPerPixel = 0.005f;
			constexpr float kMaxPitch = 1.5f; // 真上/真下の手前で止める(ビューの上方向が定まらなくなるため)
			cameraYaw_ -= event.motion.xrel * kRadiansPerPixel;
			cameraPitch_ = std::clamp(cameraPitch_ + event.motion.yrel * kRadiansPerPixel, -kMaxPitch, kMaxPitch);
		}
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		zoomCamera(event.wheel.y * 0.1f);
		break;
	case SDL_EVENT_PINCH_UPDATE:
		// トラックパッドのピンチ。scaleは前回からの変化量(1.02のような倍率か0.02のような差分のどちらでも扱えるようにする)
		zoomCamera((event.pinch.scale > 0.5f ? event.pinch.scale - 1.0f : event.pinch.scale) * 2.0f);
		break;
	default:
		break;
	}
}

// amount > 0で近づく(指数的に変えるので、遠くでも近くでも同じ感覚で動く)
void VulkanSampleScene::zoomCamera(float amount)
{
	cameraDistance_ = std::clamp(cameraDistance_ * std::exp(-amount), 0.6f, 30.0f);
}

// res/model/にあるVRMファイルの番号indexを読み込んで、表示するモデルにする(前のモデルは手放す)
void VulkanSampleScene::loadVrm(int index)
{
	vrmModel_.reset();
	for(auto &player : vrmaPlayers_){ player.reset(); }
	retargeter_.reset();
	lastVrmTick_ = 0;
	lastVrmFrame_ = 0.0f;
	vrmaStartTick_ = 0;
	vrmIndex_ = index;
	if(index < 0 || index >= static_cast<int>(vrmPaths_.size())){
		return;
	}
	auto &window = vulkanWindow(*this);
	auto &res = getResources();
	const char *path = vrmPaths_[static_cast<size_t>(index)].c_str();
	SDL_Log("VRM: %s", path);
	if(const auto data = res.loadModel(path)){
		SDL_Log("VRM: %zu vertices, %zu triangles, %zu materials, %zu images, %zu bones, %zu morphs", data->vertices.size(),
			data->indices.size() / 3, data->materials.size(), data->embeddedImages.size(), data->bones.size(), data->morphs.size());
		vrmModel_ = VulkanModel::create(window.getContext(), window.getBonePool(), window.getTexturePool(), data, [&](const std::string &imagePath){
			return window.createTexture(res.loadImage(imagePath));
		}, SDL_::VulkanWindow::kFrameSlots, [&](const std::vector<uint8_t> &bytes, bool ignoreAlpha){
			return window.createTexture(res.loadImageFromMemory(bytes, ignoreAlpha));
		});
	}
	// VRMAのモーション(あれば): 0=ダンス(VMDをvmd2vrmaで変換したもの)、1〜7=res/motion/VRMA_01〜07.vrma。VRMの再生だけで済み、MMDのモデルは要らない
	if(vrmModel_ && vrmModel_->skeleton()){
		for(int i = 0; i < kMotionCount; ++i){
			char path[64];
			if(i == 0){
				std::snprintf(path, sizeof(path), "res/motion/dance.vrma");
			}
			else if(i == 8){
				std::snprintf(path, sizeof(path), "res/motion/Walking.fbx"); // FBX(Mixamo)を、そのまま読み込む
			}
			else{
				std::snprintf(path, sizeof(path), "res/motion/VRMA_%02d.vrma", i);
			}
			if(!res.exists(path)){
				continue;
			}
			if(const auto animation = res.loadVrma(path)){
				vrmaPlayers_[static_cast<size_t>(i)] = model::VrmaPlayer::create(animation, vrmModel_->data(), *vrmModel_->skeleton(), vrmModel_->morphs());
				if(vrmaPlayers_[static_cast<size_t>(i)]){
					SDL_Log("VRMA %d: %s (%.1f s, %zu bone tracks)", i, path, animation->duration, animation->rotations.size());
				}
			}
		}
	}
	if(vrmModel_){
		vrmModel_->setAmbientBoost(0.0f); // MToonは明るく出るので、キャラの環境光の持ち上げは無し
	}
	vrmTransform_.setPos(geo::Vector3f(0.0f, -1.0f, 0.0f)); // VRMの単位はメートル。足元を床(y=-1)に置く
	vrmTransform_.setScale(geo::Vector3f(1.0f, 1.0f, 1.0f));
	// MMDのモデルとモーションがあれば、MMDの姿勢をVRMへ写す経路も使える(VRMAがあればVRMAを優先)
	if(vrmModel_ && vrmModel_->skeleton() && aquaModel_ && aquaModel_->skeleton() && aquaMotion_){
		retargeter_ = model::Retargeter::create(*aquaModel_->skeleton(), *vrmModel_->skeleton(), vrmModel_->data().humanoidBones);
	}
}

// res/model/の全ファイルを読み込んで、全モデル表示の用意をする(モーションは0=ダンス、1〜7=VRMAを、モデルごとに持つ)
// 全モデル表示の読み込みを1体ぶんだけ進める(毎フレーム1回呼ぶ)。一気に読むとイベントを処理できない時間が長くなり、
// ウィンドウが応答なしになる(クリックでの前面化もできない)ので、1フレームに1体ずつ読んで、読めたものから表示する
void VulkanSampleScene::loadAllStep()
{
	auto &window = vulkanWindow(*this);
	auto &res = getResources();
	if(allLoadPath_ == 0 && allLoadCopy_ == 0 && allActors_.empty()){
		window.raise(); // 起動直後にまず前面・キーボードフォーカスを取る(他のアプリから起動すると、取れないことがある)
	}
	if(allLoadPath_ >= vrmPaths_.size()){
		allLoaded_ = true;
		SDL_Log("VRM (all): %zu models loaded", allActors_.size());
		updateWindowTitle();
		window.raise();
		return;
	}
	const std::string &path = vrmPaths_[allLoadPath_];
	if(allLoadCopy_ == 0){
		allData_ = res.loadModel(path.c_str());
		allPathTextures_.clear();
		allEmbeddedTextures_.clear();
		if(!allData_){
			++allLoadPath_;
			return;
		}
	}
	// 同じモデルを kAllCopies 体ずつ(モデルのデータとテクスチャは共有、GPUのバッファ・ボーン・姿勢は個体ごと)
	auto actorPtr = std::make_unique<VrmActor>();
	VrmActor &actor = *actorPtr;
	actor.model = VulkanModel::create(window.getContext(), window.getBonePool(), window.getTexturePool(), allData_, [&](const std::string &imagePath){
		auto &texture = allPathTextures_[imagePath];
		if(!texture){
			texture = window.createTexture(res.loadImage(imagePath));
		}
		return texture;
	}, SDL_::VulkanWindow::kFrameSlots, [&](const std::vector<uint8_t> &bytes, bool ignoreAlpha){
		auto &texture = allEmbeddedTextures_[{bytes.data(), ignoreAlpha}];
		if(!texture){
			texture = window.createTexture(res.loadImageFromMemory(bytes, ignoreAlpha));
		}
		return texture;
	});
	if(actor.model){
		actor.model->setAmbientBoost(0.0f);
		if(actor.model->skeleton()){
			for(int i = 0; i < kMotionCount; ++i){
				char motionPath[64];
				if(i == 0){
					std::snprintf(motionPath, sizeof(motionPath), "res/motion/dance.vrma");
				}
				else if(i == 8){
					std::snprintf(motionPath, sizeof(motionPath), "res/motion/Walking.fbx");
				}
				else{
					std::snprintf(motionPath, sizeof(motionPath), "res/motion/VRMA_%02d.vrma", i);
				}
				if(!res.exists(motionPath)){
					continue;
				}
				if(const auto animation = res.loadVrma(motionPath)){
					actor.players[static_cast<size_t>(i)] = model::VrmaPlayer::create(animation, actor.model->data(), *actor.model->skeleton(), actor.model->morphs());
				}
			}
		}
		// 格子状: モデルの種類ごとに1列(奥行き。全体を中央に寄せる)、同じモデルのコピーを横へ並べる。足元は床(y=-1)
		const float centerZ = (static_cast<float>(vrmPaths_.size()) - 1.0f) * 0.5f * kAllSpacingZ;
		actor.transform.setPos(geo::Vector3f((static_cast<float>(allLoadCopy_) - (kAllCopies - 1) * 0.5f) * kAllSpacingX,
			-1.0f, static_cast<float>(allLoadPath_) * kAllSpacingZ - centerZ));
		actor.transform.setScale(geo::Vector3f(1.0f, 1.0f, 1.0f));
		allActors_.push_back(std::move(actorPtr));
	}
	if(++allLoadCopy_ >= kAllCopies){
		SDL_Log("VRM (all): %s x%d (%zu vertices, %zu bones)", path.c_str(), kAllCopies, allData_->vertices.size(), allData_->bones.size());
		allLoadCopy_ = 0;
		++allLoadPath_;
		allData_.reset();
	}
}

// 全モデル表示の1体ぶん: 再生中のモーション(無ければ休止ポーズでまばたき)を当てて、スプリングボーンまで解く
void VulkanSampleScene::updateActor(VrmActor &actor, uint32_t tick, float t)
{
	auto *skeleton = actor.model->skeleton();
	auto *morphs = actor.model->morphs();
	const auto *player = activeMotion_ >= 0 ? actor.players[static_cast<size_t>(activeMotion_)].get() : nullptr;
	float frame = 0.0f;
	if(player && skeleton){
		const float seconds = std::fmod(static_cast<float>(tick - allStartTick_) * 0.001f, player->duration() + 1e-3f);
		frame = seconds * 30.0f;
		if(morphs){
			morphs->resetWeights();
		}
		player->apply(*skeleton, morphs, seconds);
	}
	else{
		if(skeleton){
			skeleton->resetPose();
		}
		if(morphs){
			morphs->resetWeights();
			const float phase = std::fmod(t, 4.0f);
			morphs->setWeight(morphs->findMorph("blink"), phase < 0.2f ? std::sin(phase / 0.2f * 3.14159265f) : 0.0f);
		}
	}
	const float dt = actor.lastTick != 0 ? std::min(static_cast<float>(tick - actor.lastTick) * 0.001f, 0.1f) : 0.0f;
	actor.lastTick = tick;
	const bool restart = dt == 0.0f || frame < actor.lastFrame;
	actor.lastFrame = frame;
	actor.model->updatePose(restart ? 0.0f : dt);
	if(restart){
		actor.model->resetPhysics();
	}
}

// モデルの足元の位置(ワールドのx, z): 腰のボーンの真下。モーションで動くとルートは動かず腰が動くので、腰に追従する。腰が分からなければ、ルートの位置
static void footPosition(const VulkanModel &model, geo::AffineMap &transform, float &x, float &z)
{
	float hips[3];
	if(model.hipsPosition(hips)){
		const float *m = transform.getMatrix().data(); // 列優先
		x = m[0] * hips[0] + m[4] * hips[1] + m[8] * hips[2] + m[12];
		z = m[2] * hips[0] + m[6] * hips[1] + m[10] * hips[2] + m[14];
	}
	else{
		x = transform.getPos().getX();
		z = transform.getPos().getZ();
	}
}

void VulkanSampleScene::drawBlobShadow(const geo::Matrix4x4f &viewProj, float x, float z, float radius, float opacity, float height)
{
	if(!blobTexture_){
		return;
	}
	auto &window = vulkanWindow(*this);
	const float spread = 1.0f + height * 0.5f;
	geo::AffineMap model;
	model.setPos(geo::Vector3f(x, floorModel_.getPos().getY() + 0.01f, z)); // 床から少し浮かせて、床と同じ深度でちらつかないようにする
	model.setRotation(geo::Quaternionf::createRotater(-1.5707963f, geo::Vector3f(1.0f, 0.0f, 0.0f))); // 板(+Z向き)を水平(+Y向き)に寝かせる
	model.setScale(geo::Vector3f(radius * 2.0f * spread, radius * 2.0f * spread, 1.0f));
	window.drawSprite3D(blobTexture_, viewProj * model.getMatrix(), 1.0f, 1.0f, 1.0f, opacity / spread);
}

void VulkanSampleScene::updateWindowTitle()
{
	auto &window = vulkanWindow(*this);
	char title[200];
	std::snprintf(title, sizeof(title), "%sAA: %s | Culling: %s | Shadow: %s | PointShadow: 1/%d | Lamps: %zu | Light: %s | Models: %s  [B: AA, C: culling, M: shadow, P: point shadow, L: lamps, H: light, G: all models]",
		paused_ ? "[PAUSED] " : "", SDL_::VulkanWindow::antiAliasingName(window.antiAliasing()), window.frustumCulling() ? "on" : "off",
		window.shadowMapsEnabled() ? "map" : "blob", window.pointShadowInterval(), activeLamps_, cameraLight_ ? "camera" : "world", allMode_ ? std::to_string(allActors_.size()).c_str() : "1");
	SDL_SetWindowTitle(window.get(), title);
	SDL_Log("%s", title);
}

// モーション(index: 0〜7)を最初から再生する。そのVRMAが無ければ何もしない
void VulkanSampleScene::startMotion(int index)
{
	if(index < 0 || index >= kMotionCount){
		return;
	}
	bool available = vrmaPlayers_[static_cast<size_t>(index)] != nullptr;
	for(const auto &actor : allActors_){
		available = available || actor->players[static_cast<size_t>(index)] != nullptr;
	}
	if(!available){
		return;
	}
	activeMotion_ = index;
	allStartTick_ = 0;
	vrmaStartTick_ = 0;
	lastVrmTick_ = 0; // 姿勢が飛ぶので、揺れ物(スプリングボーン)を合わせ直す
}

void VulkanSampleScene::onSuspend()
{
}

void VulkanSampleScene::onCreate(uint32_t tick)
{
	auto &window = vulkanWindow(*this);
	cameraDistance_ = allMode_ ? kAllCameraDistance : kInitialCameraDistance;
	cameraPitch_ = allMode_ ? kAllCameraPitch : kInitialCameraPitch;
	// 起動時のカメラ・表示の指定(動作確認・比較用): VULKAN_ALL=0で単独表示、VULKAN_CAM=距離,注視点のy,仰角(ラジアン),水平角(ラジアン)
	// 影の既定は丸い影。VULKAN_SHADOWMAP=1でシャドウマップ
	window.setShadowMapsEnabled(SDL_getenv("VULKAN_SHADOWMAP") != nullptr && SDL_atoi(SDL_getenv("VULKAN_SHADOWMAP")) != 0);
	{
		constexpr int kBlobSize = 64;
		auto blob = std::make_shared<SDL_::Image>(kBlobSize, kBlobSize);
		for(int y = 0; y < kBlobSize; ++y){
			for(int x = 0; x < kBlobSize; ++x){
				const float dx = (static_cast<float>(x) + 0.5f) / kBlobSize * 2.0f - 1.0f;
				const float dy = (static_cast<float>(y) + 0.5f) / kBlobSize * 2.0f - 1.0f;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float t = std::clamp(1.0f - r, 0.0f, 1.0f);
				const float alpha = t * t * (3.0f - 2.0f * t); // 中心から縁へ、なめらかに0へ
				SDL_WriteSurfacePixel(blob->get(), x, y, 0, 0, 0, static_cast<Uint8>(alpha * 255.0f + 0.5f));
			}
		}
		blobTexture_ = window.createTexture(blob);
	}
	if(const char *light = SDL_getenv("VULKAN_CAMLIGHT")){
		cameraLight_ = SDL_atoi(light) != 0; // 起動時の光の向き(1でカメラ追従)
	}
	if(const char *motion = SDL_getenv("VULKAN_MOTION")){
		activeMotion_ = SDL_atoi(motion); // 起動時のモーション番号(-1で停止=休止ポーズ)
	}
	if(const char *all = SDL_getenv("VULKAN_ALL")){
		allMode_ = SDL_atoi(all) != 0;
	}
	if(const char *cam = SDL_getenv("VULKAN_CAM")){
		float distance = cameraDistance_, targetY = cameraTarget_[1], pitch = cameraPitch_, yaw = cameraYaw_;
		if(std::sscanf(cam, "%f,%f,%f,%f", &distance, &targetY, &pitch, &yaw) >= 1){
			cameraDistance_ = distance;
			cameraTarget_[1] = targetY;
			cameraPitch_ = pitch;
			cameraYaw_ = yaw;
		}
	}
	if(const char *lamps = SDL_getenv("VULKAN_LAMPS")){
		activeLamps_ = static_cast<size_t>(std::clamp(SDL_atoi(lamps), 0, static_cast<int>(kLampCount))); // 起動時の点光源の数(比較用)
	}
	updateWindowTitle();
	cubeMesh_ = vk_::createCubeMesh(window.getContext());
	lampMesh_ = vk_::createCubeMesh(window.getContext(), false);
	floorMesh_ = vk_::createFloorMesh(window.getContext(), 6.0f);
	floorModel_.setPos(geo::Vector3f(0.0f, -1.0f, 0.0f));
	floorModel_.setScale(geo::Vector3f(12.0f, 1.0f, 12.0f));
	for(auto &lamp : lampModels_){
		lamp.setScale(geo::Vector3f(0.12f, 0.12f, 0.12f));
	}

	for(auto &cube : cubeModels_){
		cube.setScale(geo::Vector3f(0.3f, 0.3f, 0.3f));
	}

	// PMXモデル(モデルファイルとテクスチャの読み込みはResourcesの責務)。MMDの単位は1=約8cmなので縮小し、足元を床(y=-1)に置く
	{
		auto &res = getResources();
		std::shared_ptr<const model::Motion> motionData;
		// MMDのモデルは、データ無し(再配布禁止のため置いていない)でもよい。あるときだけ読む
		if(const auto data = res.exists("res/model/minato_aqua/湊あくあ.pmx") ? res.loadModel("res/model/minato_aqua/湊あくあ.pmx") : nullptr){
			SDL_Log("PMX: %zu vertices, %zu triangles, %zu materials, %zu textures", data->vertices.size(),
				data->indices.size() / 3, data->materials.size(), data->texturePaths.size());
			size_t ikCount = 0, appendCount = 0;
			for(const auto &b : data->bones){
				ikCount += (b.flags & model::ModelBone::IK) ? 1 : 0;
				appendCount += (b.flags & (model::ModelBone::AppendRotation | model::ModelBone::AppendTranslation)) ? 1 : 0;
			}
			SDL_Log("PMX bones: %zu (IK %zu, append %zu)", data->bones.size(), ikCount, appendCount);
			// モーション: 先に見つかったものを使う(回る空うさぎ.vmd → dance.vmd → 動作確認用のtest.vmd(tools/make_test_vmd.pyで生成))
			for(const char *motionPath : {"res/motion/回る空うさぎ.vmd", "res/motion/dance.vmd", "res/motion/test.vmd"}){
				if(res.exists(motionPath)){
					motionData = res.loadMotion(motionPath);
					SDL_Log("VMD: %s", motionPath);
					break;
				}
			}
			aquaModel_ = VulkanModel::create(window.getContext(), window.getBonePool(), window.getTexturePool(), data, [&](const std::string &path){
				return window.createTexture(res.loadImage(path));
			}, SDL_::VulkanWindow::kFrameSlots);
		}
		if(aquaModel_){
			aquaModel_->setAmbientBoost(0.1f); // 暗い色の衣装と自己影で沈みやすいので、キャラだけ環境光を持ち上げる
		}
		if(motionData && aquaModel_ && aquaModel_->skeleton()){
			aquaMotion_ = std::make_unique<model::MotionPlayer>(motionData, *aquaModel_->skeleton(), aquaModel_->morphs());
			if(aquaModel_->morphs()){
				SDL_Log("VMD: %zu morph tracks (%zu bound), model has %zu morphs", motionData->morphTracks.size(), aquaMotion_->boundMorphTrackCount(), aquaModel_->morphs()->count());
				std::string unboundMorphs;
				for(const auto &name : aquaMotion_->unboundMorphTrackNames()){
					unboundMorphs += name + " ";
				}
				SDL_Log("VMD morph tracks without a morph in the model: %s", unboundMorphs.c_str());
			}
			SDL_Log("VMD: %zu tracks (%zu bound), last frame %u", motionData->tracks.size(), aquaMotion_->boundTrackCount(), motionData->lastFrame);
			std::string unbound;
			for(const auto &name : aquaMotion_->unboundTrackNames()){
				unbound += name + " ";
			}
			SDL_Log("VMD tracks without a bone in the model: %s", unbound.c_str());
		}
		aquaTransform_.setPos(geo::Vector3f(0.0f, -1.0f, 0.0f));
		aquaTransform_.setScale(geo::Vector3f(0.07f, 0.07f, 0.07f));
	}

	// 材質: オブジェクトごとにテクスチャと鏡面反射を変える。画像ファイルはResourcesが読み、
	// 読めなければ市松模様で代用する
	auto &res = getResources();
	const auto checker = createChecker(256, 32);
	const auto fallback = [&](const std::shared_ptr<SDL_::Image> &image){ return image ? image : checker; };
	const auto texTex1 = window.createTexture(fallback(res.loadImage("res/image/tex1.bmp")));
	floorMaterial_ = {window.createTexture(checker), 0.1f, 16.0f, false, true}; // 床: 市松。影を受けるだけで、影は落とさない
	for(size_t i = 0; i < lampMaterials_.size(); ++i){
		lampMaterials_[i] = {nullptr, 0.0f, 1.0f, false, false, static_cast<int>(i)}; // ランプ(自発光): 光源iの色で光る。影は落とさず、受けない
	}
	cubeMaterials_[0] = {window.createTexture(checker), 1.0f, 96.0f}; // 立方体0: 市松。強くて鋭いハイライト(ピカピカ)
	cubeMaterials_[1] = {texTex1, 0.0f, 1.0f};                    // 立方体1: tex1。つや無し(鏡面反射なし)
	cubeMaterials_[2] = {window.createTexture(fallback(res.loadImage(
		"res/image/iconset-music-genre-icons-by-sirubico/Jazz Icon/Jazz-icon-256.png"))), 0.6f, 32.0f}; // 立方体2: PNGアイコン
}

bool VulkanSampleScene::onIdle(uint32_t tick)
{
	Scene::onIdle(tick);
	auto &window = vulkanWindow(*this);
	window.setClearColor(0.0f, 0.1f, 0.3f);

	// 起動からN ms後に自動で一時停止する(VULKAN_PAUSE_AFTER=N。動作確認・キャプチャ用)
	static const int pauseAfter = SDL_getenv("VULKAN_PAUSE_AFTER") ? SDL_atoi(SDL_getenv("VULKAN_PAUSE_AFTER")) : -1;
	static bool pauseAfterDone = false;
	if(pauseAfter >= 0 && !pauseAfterDone && static_cast<int>(tick) >= pauseAfter && (allLoaded_ || !allMode_)){
		pauseAfterDone = true;
		pausedAt_ = tick;
		paused_ = true;
		updateWindowTitle();
	}

	// シーンの時刻(アニメーション用)は、止めていた時間を引いたもの。カメラ操作は実時刻(realTick)で動かす
	const uint32_t realTick = tick;
	tick = (paused_ ? pausedAt_ : realTick) - pausedTotal_;

	const float t = static_cast<float>(tick) * 0.001f;
	const float pi = 3.14159265f;

	// カメラ(全オブジェクト共通): キャラの中心(注視点)の周りを、水平角/仰角/距離で回る
	// W/A/S/Dキー(押している間): 注視点を、いまのカメラの向きに合わせて、前(W)・後ろ(S)・左(A)・右(D)へ水平に動かす(速さは距離に比例)
	{
		const float dt = lastCameraTick_ != 0 ? std::min(static_cast<float>(realTick - lastCameraTick_) * 0.001f, 0.1f) : 0.0f;
		lastCameraTick_ = realTick;
		const bool *keys = SDL_GetKeyboardState(nullptr);
		const float right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
		const float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
		const float speed = cameraDistance_ * 0.6f * dt;
		// カメラの右方向(水平)と前方向(水平。カメラから注視点へ向かう向き)
		cameraTarget_[0] += (std::cos(cameraYaw_) * right - std::sin(cameraYaw_) * forward) * speed;
		cameraTarget_[2] += (-std::sin(cameraYaw_) * right - std::cos(cameraYaw_) * forward) * speed;
	}
	const geo::Vector3f target(cameraTarget_[0], cameraTarget_[1], cameraTarget_[2]);
	const geo::Vector3f eyePos = target + geo::Vector3f(std::cos(cameraPitch_) * std::sin(cameraYaw_), std::sin(cameraPitch_),
		std::cos(cameraPitch_) * std::cos(cameraYaw_)) * cameraDistance_;
	const auto view = geo::createLookAt<float>(eyePos, target, {0.0f, 1.0f, 0.0f});
	const auto proj = vk_::createPerspective(pi / 3.0f, window.getScreenWidth(), window.getScreenHeight(), 0.1f, 100.0f);
	const auto viewProj = proj * view;

	window.setCameraPosition(eyePos);
	window.setToonShading(toonShading_);

	// 平行光源: 右上手前から左下奥へ進む弱めの光 + 環境光(ワールド空間で固定)
	window.setAmbientEnvironment(true, geo::Vector3f(0.36f, 0.32f, 0.27f), -1.0f, 0.5f, 0.6f); // 半球の環境光(床からの照り返し)・接地AO・まわり込み
	// 光の向き: 既定はワールドで固定(右上手前から)。Hキーでカメラ追従にすると、カメラから見て右上手前から当たる
	// (どの角度から見ても、見えている面が正面に近い向きから照らされる。キャラの顔の見え方の確認用)
	geo::Vector3f lightDirection(-0.5f, -1.0f, -0.7f);
	if(cameraLight_){
		const geo::Vector3f forward = geo::Vector3f::normalize(target - eyePos);
		const geo::Vector3f right = geo::Vector3f::normalize(geo::Vector3f(-forward.getZ(), 0.0f, forward.getX())); // 水平の右
		lightDirection = geo::Vector3f::normalize(forward * 1.0f + geo::Vector3f(0.0f, -0.6f, 0.0f) + right * -0.35f);
	}
	window.setLight(lightDirection, 0.55f, 0.55f, 0.55f, 0.35f);

	// 点光源3つ: 色と軌道(半径・速さ・高さ)がそれぞれ違う。色=暖色/青/緑。各光源が別々の影を落とす
	struct Orbit { float radius, speed, phase, height, bob; geo::Vector3f color; };
	const Orbit orbits[kLampCount] = {
		{1.1f, -1.1f, 0.0f, 0.7f, 0.3f, geo::Vector3f(1.0f, 0.8f, 0.5f)},
		{2.2f,  0.7f, 2.1f, 0.5f, 0.2f, geo::Vector3f(0.4f, 0.6f, 1.0f)},
		{1.6f, -0.5f, 4.2f, 1.1f, 0.1f, geo::Vector3f(0.5f, 1.0f, 0.6f)},
	};
	for(size_t i = 0; i < activeLamps_; ++i){ // 点灯数はLキーで変える(0〜kLampCount)
		const auto &o = orbits[i];
		const float a = t * o.speed + o.phase;
		const geo::Vector3f pos(std::cos(a) * o.radius, o.height + o.bob * std::sin(t * 1.3f + o.phase), std::sin(a) * o.radius);
		window.setPointLight(static_cast<int>(i), pos, o.color.getX(), o.color.getY(), o.color.getZ());
		lampModels_[i].setPos(pos);
		window.draw(lampMesh_, viewProj * lampModels_[i].getMatrix(), lampModels_[i].getMatrix(), lampMaterials_[i]);
	}

	// 床。平行光源のシャドウマップは、十字・立方体・床が入る範囲(中心と半径)に合わせる
	window.setShadowArea(geo::Vector3f(0.0f, -0.3f, 0.0f), 9.0f);
	// 床は両面描画だが、カメラが床より下にあるときは描かない(下から見て床が邪魔にならないよう、裏面を省く)
	if(eyePos.getY() >= floorModel_.getPos().getY()){
		window.draw(floorMesh_, viewProj * floorModel_.getMatrix(), floorModel_.getMatrix(), floorMaterial_);
	}

	// 立方体: 十字の周りを公転しつつ、斜めの軸で自転(位相をずらして配置)
	for(size_t i = 0; i < cubeModels_.size(); ++i){
		const float phase = t * 0.8f + static_cast<float>(i) * 2.0f * pi / static_cast<float>(cubeModels_.size());
		auto &cube = cubeModels_[i];
		cube.setPos(geo::Vector3f(std::cos(phase) * 1.6f, std::sin(t * 0.7f + static_cast<float>(i)) * 0.4f, std::sin(phase) * 1.6f));
		cube.setRotation(geo::Quaternionf::createRotater(t * 1.5f, geo::Vector3f::normalize(geo::Vector3f(1.0f, 1.0f, 0.0f))));
		window.draw(cubeMesh_, viewProj * cube.getMatrix(), cube.getMatrix(), cubeMaterials_[i]);
	}

	// VRMモデル(Vキーで切り替え)。MMDのモーションがあれば、MMDモデル(aquaModel_)に当てた姿勢をVRMへ写して踊らせる
	// (対応づけできなければ、休止ポーズ(Tポーズ)のままで、まばたきだけする)
	if(!vrmListed_){
		// 初回: res/model/のVRMファイルを調べて、最初に表示するもの(res/model/test2.vrmがあればそれ、無ければ最初のもの)を選ぶ
		vrmListed_ = true;
		vrmPaths_ = getResources().listFiles("res/model", "*.vrm");
		for(size_t i = 0; i < vrmPaths_.size(); ++i){
			if(pendingVrmSlot_ < 0 || vrmPaths_[i] == "res/model/test2.vrm"){
				pendingVrmSlot_ = static_cast<int>(i);
			}
		}
		if(const char *vrm = SDL_getenv("VULKAN_VRM")){
			pendingVrmSlot_ = std::clamp(SDL_atoi(vrm), 0, std::max(0, static_cast<int>(vrmPaths_.size()) - 1)); // 起動時に表示するVRMの番号(比較用)
		}
		if(pendingVrmSlot_ < 0){
			SDL_Log("VRM: no model found in res/model/");
		}
	}
	if(allMode_ && !allLoaded_ && vrmListed_){
		loadAllStep();
	}
	if(pendingVrmSlot_ >= 0 && !allMode_){
		const int slot = pendingVrmSlot_;
		pendingVrmSlot_ = -1;
		if(slot < static_cast<int>(vrmPaths_.size())){
			showVrm_ = true;
			loadVrm(slot);
		}
		else{
			showVrm_ = false; // MMD
			loadVrm(-1);
		}
	}
	if(allMode_){
		if(allStartTick_ == 0){
			allStartTick_ = tick; // 全員のモーションの開始をそろえる(並列に更新する前に決めておく)
		}
		// 姿勢の更新(モーション・スプリングボーン・モーフ)はモデルごとに独立なので、複数のスレッドで分けて進める。
		// (CPUの負荷の大半はスプリングボーンの物理演算なので、40体だとここがボトルネックになる)。描画の予約(draw)は1スレッドで順に行う
		const size_t count = paused_ ? 0 : allActors_.size(); // 一時停止中は更新しない(描画の予約は下で全員ぶん行う)
		const size_t workers = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), count);
		std::vector<std::future<void>> tasks;
		for(size_t w = 1; w < workers; ++w){
			tasks.push_back(std::async(std::launch::async, [&, w]{
				for(size_t i = w; i < count; i += workers){
					updateActor(*allActors_[i], tick, t);
				}
			}));
		}
		for(size_t i = 0; i < count; i += std::max<size_t>(workers, 1)){
			updateActor(*allActors_[i], tick, t);
		}
		for(auto &task : tasks){
			task.get();
		}
		for(auto &actor : allActors_){
			window.draw(actor->model, viewProj, actor->transform.getMatrix());
		}
	}
	if(!allMode_ && showVrm_ && vrmModel_){
		auto *vrmSkeleton = vrmModel_->skeleton();
		auto *vrmMorphs = vrmModel_->morphs();
		if(!paused_){ // 一時停止中は、姿勢を更新しない(物理演算の揺れもそのまま)
		float vrmFrame = 0.0f;
		const auto *vrmaPlayer = activeMotion_ >= 0 ? vrmaPlayers_[static_cast<size_t>(activeMotion_)].get() : nullptr;
		bool anyVrma = false;
		for(const auto &player : vrmaPlayers_){ anyVrma = anyVrma || player; }
		if(vrmaPlayer && vrmSkeleton){
			// VRMAのモーション: 時刻の姿勢をそのまま当てる(ループ)
			if(vrmaStartTick_ == 0){
				vrmaStartTick_ = tick;
			}
			const float seconds = std::fmod(static_cast<float>(tick - vrmaStartTick_) * 0.001f, vrmaPlayer->duration() + 1e-3f);
			vrmFrame = seconds * 30.0f;
			if(vrmMorphs){
				vrmMorphs->resetWeights();
			}
			vrmaPlayer->apply(*vrmSkeleton, vrmMorphs, seconds);
		}
		else if(!anyVrma && retargeter_ && vrmSkeleton){
			// MMD側の姿勢(IK・付与まで解く。物理演算は要らない)
			auto *mmdSkeleton = aquaModel_->skeleton();
			mmdSkeleton->resetPose();
			if(!motionStarted_){
				motionStarted_ = true;
				motionStartTick_ = tick;
			}
			const float seconds = static_cast<float>(tick - motionStartTick_) * 0.001f;
			const float frame = std::fmod(seconds * 30.0f, aquaMotion_->lastFrame() + 1.0f);
			vrmFrame = frame;
			aquaMotion_->apply(*mmdSkeleton, frame);
			mmdSkeleton->update();
			retargeter_->apply(*mmdSkeleton, *vrmSkeleton);
			// 表情: MMDのモーフ名をVRMの表情に写す
			if(vrmMorphs){
				vrmMorphs->resetWeights();
				if(auto *mmdMorphs = aquaModel_->morphs()){
					mmdMorphs->resetWeights();
					aquaMotion_->applyMorphs(*mmdMorphs, frame);
					static const std::pair<const char *, const char *> kExpressions[] = {
						{"まばたき", "blink"}, {"あ", "aa"}, {"い", "ih"}, {"う", "ou"}, {"え", "ee"}, {"お", "oh"},
						{"笑い", "happy"}, {"怒り", "angry"}, {"困る", "sad"}, {"驚き", "surprised"},
					};
					for(const auto &entry : kExpressions){
						const int from = mmdMorphs->findMorph(entry.first);
						if(from >= 0){
							vrmMorphs->setWeight(vrmMorphs->findMorph(entry.second), mmdMorphs->weight(from));
						}
					}
				}
			}
		}
		else{
			if(vrmSkeleton){
				vrmSkeleton->resetPose();
			}
			if(vrmMorphs){
				vrmMorphs->resetWeights();
				// 4秒に1回、0.2秒かけてまばたき(閉じて開く)
				const float phase = std::fmod(t, 4.0f);
				const float blink = phase < 0.2f ? std::sin(phase / 0.2f * pi) : 0.0f;
				vrmMorphs->setWeight(vrmMorphs->findMorph("blink"), blink);
			}
		}
		// スプリングボーン(髪やスカートの揺れ)。最初と、モーションがループして戻った時は、揺れを止めて姿勢へ合わせ直す
		const float vrmDt = lastVrmTick_ != 0 ? std::min(static_cast<float>(tick - lastVrmTick_) * 0.001f, 0.1f) : 0.0f;
		lastVrmTick_ = tick;
		const bool vrmRestart = vrmDt == 0.0f || vrmFrame < lastVrmFrame_;
		lastVrmFrame_ = vrmFrame;
		vrmModel_->updatePose(vrmRestart ? 0.0f : vrmDt);
		if(vrmRestart){
			vrmModel_->resetPhysics();
		}
		}
		window.draw(vrmModel_, viewProj, vrmTransform_.getMatrix());
	}

	// 中央のPMXモデル: Y軸でゆっくり自転。半透明の材質があるので、不透明な物体より後に描く
	if(!allMode_ && aquaModel_ && !showVrm_){
		if(!paused_){
		// 手動のテストポーズ(モーションが無いときのデモ): 腕を振り、頭を傾け、センターを上下させ、左足IKを持ち上げる
		if(auto *skeleton = aquaModel_->skeleton(); skeleton && aquaMotion_){
			// モーション再生(30フレーム/秒でループ)
			skeleton->resetPose();
			if(!motionStarted_){
				motionStarted_ = true;
				motionStartTick_ = tick; // 読み込みが終わった最初のフレームから再生する
			}
			const float seconds = static_cast<float>(tick - motionStartTick_) * 0.001f;
			const float frame = std::fmod(seconds * 30.0f, aquaMotion_->lastFrame() + 1.0f);
			aquaMotion_->apply(*skeleton, frame);
			if(auto *morphs = aquaModel_->morphs()){
				morphs->resetWeights();
				aquaMotion_->applyMorphs(*morphs, frame);
			}
			// 物理演算(髪やスカートの揺れ)。モーションの頭出し(最初と、ループして戻った時)は、揺れを止めて姿勢へ合わせ直す
			const float dt = lastPhysicsTick_ != 0 ? std::min(static_cast<float>(tick - lastPhysicsTick_) * 0.001f, 0.1f) : 0.0f;
			lastPhysicsTick_ = tick;
			const bool restart = dt == 0.0f || frame < lastMotionFrame_;
			lastMotionFrame_ = frame;
			aquaModel_->updatePose(restart ? 0.0f : dt);
			if(restart){
				aquaModel_->resetPhysics();
			}
		}
		else if(skeleton){
			skeleton->resetPose();
			auto rotate = [&](const char *name, const model::Vec3 &axis, float radians){
				const int bone = skeleton->findBone(name);
				if(bone >= 0){
					skeleton->setBoneRotation(bone, model::Quat::fromAxisAngle(axis, radians));
				}
			};
			const float swing = std::sin(t * 2.0f);
			rotate("左腕", {0.0f, 0.0f, 1.0f}, 0.7f * swing);
			rotate("右腕", {0.0f, 0.0f, 1.0f}, 0.7f * swing);
			rotate("頭", {1.0f, 0.0f, 0.0f}, 0.25f * swing);
			const int center = skeleton->findBone("センター");
			if(center >= 0){
				skeleton->setBoneTranslation(center, {0.0f, -0.6f * std::fabs(swing), 0.0f});
			}
			const int leftIk = skeleton->findBone("左足ＩＫ");
			if(leftIk >= 0){
				skeleton->setBoneTranslation(leftIk, {0.0f, 3.0f * std::max(swing, 0.0f), 0.0f});
			}
			aquaModel_->updatePose();
		}
		} // !paused_
		// モーションがあるときは、ダンスの向きに任せる(自転させない)。無いときだけゆっくり自転
		aquaTransform_.setRotation(geo::Quaternionf::createRotater(aquaMotion_ ? 0.0f : t * 0.5f, geo::Vector3f(0.0f, 1.0f, 0.0f)));
		window.draw(aquaModel_, viewProj, aquaTransform_.getMatrix());
	}

	// 丸い影(シャドウマップを使わないとき): 足元と、浮いている立方体の真下
	if(!window.shadowMapsEnabled()){
		if(allMode_){
			for(const auto &actor : allActors_){
				float x, z;
				footPosition(*actor->model, actor->transform, x, z);
				drawBlobShadow(viewProj, x, z, 0.5f, 0.55f);
			}
		}
		else if(showVrm_ && vrmModel_){
			float x, z;
			footPosition(*vrmModel_, vrmTransform_, x, z);
			drawBlobShadow(viewProj, x, z, 0.5f, 0.55f);
		}
		else if(aquaModel_){
			float x, z;
			footPosition(*aquaModel_, aquaTransform_, x, z);
			drawBlobShadow(viewProj, x, z, 0.5f, 0.55f);
		}
		for(const auto &cube : cubeModels_){
			drawBlobShadow(viewProj, cube.getPos().getX(), cube.getPos().getZ(), 0.3f, 0.5f, cube.getPos().getY() - floorModel_.getPos().getY());
		}
	}

	window.setTint(1.0f, 1.0f, 1.0f);

	swap();
	return true; // アニメーションのため常時再描画する
}
