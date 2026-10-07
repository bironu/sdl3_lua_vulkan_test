#include "scene/characterselect/CharacterSelectScene.h"
#include "geo/AffineMap.h"
#include "geo/Calculator.h"
#include "resources/Resources.h"
#include "scene/common/ModelFactory.h"
#include "scene/game/GameSession.h"
#include "sdl/SDLVulkanWindow.h"
#include "vk/VulkanMath.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace game
{

namespace
{
constexpr float kPi = 3.14159265f;
constexpr float kRingRadiusX = 2.7f; // 円(を横に見た楕円)の、横と奥行きの半径(メートル)
constexpr float kRingRadiusZ = 2.2f;
constexpr float kRotateRate = 11.0f; // 回転が目標へ近づく速さ(1/秒。大きいほど速い)
}

CharacterSelectScene::~CharacterSelectScene()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
	}
	wake_.notify_all();
	if(worker_.joinable()){
		worker_.join();
	}
}

int CharacterSelectScene::current() const
{
	const int count = static_cast<int>(slots_.size());
	return count == 0 ? 0 : ((target_ % count) + count) % count;
}

void CharacterSelectScene::onCreate(uint32_t tick)
{
	LuaUiScene::onCreate(tick);
	auto &window = static_cast<SDL_::VulkanWindow &>(getWindow());
	window.setClearColor(0.10f, 0.12f, 0.20f);
	window.setShadowMapsEnabled(false);
	window.clearPointLights();
	window.setAmbientEnvironment(true, geo::Vector3f(0.36f, 0.32f, 0.27f), 0.0f, 0.5f, 0.6f);
	window.setLight(geo::Vector3f(-0.3f, -0.8f, -0.7f), 0.7f, 0.7f, 0.7f, 0.4f);
	blob_ = std::make_unique<BlobShadow>(window);

	for(auto &info : loadCharacterList(kCharacterList)){
		auto slot = std::make_unique<Slot>();
		slot->info = std::move(info);
		if(slot->info.motion.empty()){
			slot->info.motion = kDefaultMotion;
		}
		slots_.push_back(std::move(slot));
	}
	if(slots_.empty()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "no characters: %s", kCharacterList);
	}
	// 前回選んだキャラ(タイトルから戻ってきたとき)が正面に来るように
	const std::string &last = GameSession::instance().character;
	for(size_t i = 0; i < slots_.size(); ++i){
		if(slots_[i]->info.model == last){
			target_ = static_cast<int>(i);
			rotation_ = static_cast<float>(i);
		}
	}
	lastTick_ = tick;

	preload_ = std::make_unique<ResourceSet>(getResources());
	worker_ = std::thread([this]{ workerLoop(); });
	requestAroundCurrent();
}

void CharacterSelectScene::request(int index, bool urgent)
{
	if(index < 0 || index >= static_cast<int>(slots_.size())){
		return;
	}
	int expected = kNotRequested;
	if(!slots_[index]->state.compare_exchange_strong(expected, kQueued)){
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(urgent){
			queue_.push_front(index);
		}
		else{
			queue_.push_back(index);
		}
	}
	wake_.notify_one();
}

// 選択中(先に)と、その両隣を読む
void CharacterSelectScene::requestAroundCurrent()
{
	const int count = static_cast<int>(slots_.size());
	if(count == 0){
		return;
	}
	const int now = current();
	request((now + 1) % count, false);
	request((now + count - 1) % count, false);
	request(now, true);
}

void CharacterSelectScene::workerLoop()
{
	for(;;){
		int index;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [this]{ return stop_ || !queue_.empty(); });
			if(stop_){
				return;
			}
			index = queue_.front();
			queue_.pop_front();
		}
		Slot &slot = *slots_[index];
		// ファイルの読み込みと、埋め込み画像のデコード(重い部分)。GPUの資源は、メインスレッドで作る
		const auto data = preload_->modelWithImages(slot.info.model);
		preload_->animation(slot.info.motion);
		slot.state = data ? kDataReady : kFailed;
	}
}

void CharacterSelectScene::createGpuResources(Slot &slot)
{
	auto &window = static_cast<SDL_::VulkanWindow &>(getWindow());
	slot.model = createVulkanModel(window, resources(), slot.info.model);
	if(!slot.model){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create the model: %s", slot.info.model.c_str());
		slot.state = kFailed;
		return;
	}
	slot.model->setAmbientBoost(0.0f);
	if(slot.model->skeleton()){
		if(const auto animation = resources().animation(slot.info.motion)){
			slot.player = model::VrmaPlayer::create(animation, slot.model->data(), *slot.model->skeleton(), slot.model->morphs());
			if(slot.player){
				slot.player->setInPlace(true);
			}
		}
	}
	slot.state = kReady;
}

void CharacterSelectScene::onCommand(const std::string &name, double value)
{
	if(slots_.empty() || confirmed_){
		return;
	}
	if(name == "rotate"){
		target_ += static_cast<int>(value);
		requestAroundCurrent(); // 回すたびに、まだ読んでいないものを読む
	}
	else if(name == "confirm"){
		Slot &slot = *slots_[current()];
		if(slot.state != kReady){
			return; // 読み込み中は決定できない
		}
		GameSession &session = GameSession::instance();
		session.character = slot.info.model;
		slot.player.reset();
		session.model = std::move(slot.model); // GPU上のモデルを、GameSceneへ引き継ぐ
		slot.state = kNotRequested;
		confirmed_ = true;
		changeScene("game");
	}
}

void CharacterSelectScene::onFrame(uint32_t tick)
{
	if(slots_.empty() || isFinished()){
		return;
	}
	auto &window = static_cast<SDL_::VulkanWindow &>(getWindow());
	const float dt = std::min(static_cast<float>(tick - lastTick_) * 0.001f, 0.1f);
	lastTick_ = tick;
	const int count = static_cast<int>(slots_.size());

	// 読み終わったデータから、GPUの資源を1フレームに1体だけ作る(選択中のものを先に)
	{
		int order[16];
		int n = 0;
		order[n++] = current();
		for(int i = 0; i < count && n < 16; ++i){
			if(i != current()){
				order[n++] = i;
			}
		}
		for(int i = 0; i < n; ++i){
			Slot &slot = *slots_[order[i]];
			if(slot.state == kDataReady){
				createGpuResources(slot);
				break;
			}
		}
	}

	// 回転: 目標へ滑らかに(最短ではなく、回した向きのまま)
	rotation_ += (static_cast<float>(target_) - rotation_) * (1.0f - std::exp(-kRotateRate * dt));

	// カメラ: 少し上から、正面のキャラを見る
	const geo::Vector3f eye(0.0f, 1.9f, 4.6f);
	const geo::Vector3f lookAt(0.0f, 0.95f, -0.6f);
	const auto view = geo::createLookAt<float>(eye, lookAt, {0.0f, 1.0f, 0.0f});
	const auto proj = vk_::createPerspective(kPi / 4.5f, window.getScreenWidth(), window.getScreenHeight(), 0.1f, 50.0f);
	const auto viewProj = proj * view;
	window.setCameraPosition(eye);

	// 奥のものから描く(足元の影は半透明なので、モデルより先に、全員分を奥から)
	struct Placed
	{
		int index;
		float x, z;
	};
	std::vector<Placed> placed;
	for(int i = 0; i < count; ++i){
		const float angle = (static_cast<float>(i) - rotation_) * 2.0f * kPi / static_cast<float>(count);
		placed.push_back({i, std::sin(angle) * kRingRadiusX, (std::cos(angle) - 1.0f) * kRingRadiusZ});
	}
	std::sort(placed.begin(), placed.end(), [](const Placed &a, const Placed &b){ return a.z < b.z; });
	for(const auto &p : placed){
		Slot &slot = *slots_[p.index];
		blob_->draw(window, viewProj, p.x, 0.0f, p.z, 0.55f, 0.55f);
		if(slot.state != kReady || !slot.model){
			continue;
		}
		if(auto *skeleton = slot.model->skeleton()){
			auto *morphs = slot.model->morphs();
			skeleton->resetPose();
			if(morphs){
				morphs->resetWeights();
			}
			if(slot.player){
				slot.time = std::fmod(slot.time + dt, slot.player->duration() + 1e-3f);
				slot.player->apply(*skeleton, morphs, slot.time);
			}
			slot.model->updatePose(dt);
		}
		geo::AffineMap transform;
		transform.setPos(geo::Vector3f(p.x, 0.0f, p.z));
		window.draw(slot.model, viewProj, transform.getMatrix());
	}

	// 画面の部品(Lua)へ、状態を渡す
	auto &world = uiContext().world();
	const Slot &selected = *slots_[current()];
	world.values["selected"] = static_cast<float>(current() + 1);
	world.values["count"] = static_cast<float>(count);
	world.values["ready"] = selected.state == kReady ? 1.0f : 0.0f;
	world.values["failed"] = selected.state == kFailed ? 1.0f : 0.0f;
}

} // namespace game
