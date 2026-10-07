#include "FieldEditorScene.h"
#include "geo/AffineMap.h"
#include "geo/Calculator.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "resources/Resources.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include "ui/PadNames.h"
#include "vk/PrimitiveMeshes.h"
#include "vk/VulkanMath.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
SDL_::VulkanWindow &vulkanWindow(Scene &scene)
{
	return static_cast<SDL_::VulkanWindow &>(scene.getWindow());
}

std::shared_ptr<SDL_::Image> solidImage(uint8_t r, uint8_t g, uint8_t b)
{
	auto image = std::make_shared<SDL_::Image>(1, 1);
	image->fillRect(SDL_::Color(r, g, b, 255));
	return image;
}
}

FieldEditorScene::FieldEditorScene() = default;

FieldEditorScene::~FieldEditorScene() = default;

// 保存先: 開発用のツールなので、ビルド元のリポジトリの res/field/ へ直接書く(ビルドディレクトリのコピーではなく)
std::string FieldEditorScene::filePath() const
{
#if defined(APP_RESOURCE_ROOT)
	return std::string(APP_RESOURCE_ROOT) + settings_.field;
#else
	return ResourcePaths::resource(settings_.field.c_str());
#endif
}

void FieldEditorScene::onCreate(uint32_t tick)
{
	Scene::onCreate(tick);
	auto &window = vulkanWindow(*this);
	auto &res = getResources();
	window.setScreenSize(static_cast<float>(res.getScreenWidth()), static_cast<float>(res.getScreenHeight()));
	window.setClearColor(0.16f, 0.18f, 0.22f);
	window.setShadowMapsEnabled(false);
	window.clearPointLights();
	window.setAmbientEnvironment(true, geo::Vector3f(0.5f, 0.5f, 0.5f), 0.0f, 0.5f, 0.6f);
	window.setLight(geo::Vector3f(-0.3f, -1.0f, -0.4f), 0.6f, 0.6f, 0.6f, 0.5f);

	settings_ = field::loadFieldSettings(); // 使うファイル名は、res/lua/data/field_settings.lua
	tiles_ = field::loadTileDefs(settings_.tiles);
	if(tiles_.empty()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "no tile definitions: %s", settings_.tiles.c_str());
		quit();
		return;
	}
	for(const auto &tile : tiles_){
		VulkanMaterial material;
		material.texture = window.createTexture(solidImage(tile.r, tile.g, tile.b));
		material.specular = 0.0f;
		material.castShadow = false;
		material.receiveShadow = false;
		tileMaterials_.push_back(material);
	}
	baseMaterial_.texture = window.createTexture(solidImage(24, 26, 32));
	baseMaterial_.specular = 0.0f;
	baseMaterial_.castShadow = false;
	baseMaterial_.receiveShadow = false;
	quad_ = vk_::createFloorMesh(window.getContext(), 1.0f);
	white_ = window.createTexture(solidImage(255, 255, 255));

	reload();
	targetX_ = map_.width() * map_.cellSize() * 0.5f;
	targetZ_ = map_.depth() * map_.cellSize() * 0.5f;

	uiContext_ = std::make_unique<ui::UiContext>(window, getResources(), resources());
	ui::UiScript::Callbacks callbacks;
	callbacks.quit = [this]{ quit(); };
	callbacks.command = [this](const std::string &name, double value){ command(name, value); };
	ui_ = std::make_unique<ui::UiScript>(*uiContext_, std::move(callbacks));
	ui_->load(kUiScript);
	lastTick_ = startTick_ = tick;
}

void FieldEditorScene::toScreen(float windowX, float windowY, float &x, float &y)
{
	const auto size = getWindow().getSize();
	x = windowX / static_cast<float>(std::max(size.getX(), 1)) * uiContext_->screenWidth();
	y = windowY / static_cast<float>(std::max(size.getY(), 1)) * uiContext_->screenHeight();
}

bool FieldEditorScene::pickCell(float windowX, float windowY, int &cellX, int &cellZ) const
{
	const auto size = const_cast<FieldEditorScene *>(this)->getWindow().getSize();
	const float nx = windowX / static_cast<float>(std::max(size.getX(), 1)) * 2.0f - 1.0f;
	const float ny = 1.0f - windowY / static_cast<float>(std::max(size.getY(), 1)) * 2.0f; // 上が+
	const float aspect = static_cast<float>(size.getX()) / static_cast<float>(std::max(size.getY(), 1));
	// カメラの座標系(eye → target を前、世界の上を+Yとして、右・上を求める)
	const geo::Vector3f target(targetX_, 0.0f, targetZ_);
	const geo::Vector3f forward = geo::Vector3f::normalize(target - eye_);
	const geo::Vector3f right = geo::Vector3f::normalize(geo::Vector3f::cross(forward, geo::Vector3f(0.0f, 1.0f, 0.0f)));
	const geo::Vector3f up = geo::Vector3f::cross(right, forward);
	const float tanHalf = std::tan(fovY_ * 0.5f);
	const geo::Vector3f dir = forward + right * (nx * tanHalf * aspect) + up * (ny * tanHalf);
	if(dir.getY() >= -1e-5f){
		return false; // 地平線より上
	}
	const float t = -eye_.getY() / dir.getY();
	const float wx = eye_.getX() + dir.getX() * t, wz = eye_.getZ() + dir.getZ() * t;
	cellX = static_cast<int>(std::floor(wx / map_.cellSize()));
	cellZ = static_cast<int>(std::floor(wz / map_.cellSize()));
	return map_.inside(cellX, cellZ);
}

void FieldEditorScene::paint(int cellX, int cellZ)
{
	const uint8_t before = map_.get(cellX, cellZ);
	if(map_.set(cellX, cellZ, static_cast<uint8_t>(brush_))){
		stroke_.push_back({cellX, cellZ, before});
		dirty_ = true;
	}
}

void FieldEditorScene::endStroke()
{
	painting_ = false;
	if(!stroke_.empty()){
		undoStack_.push_back(std::move(stroke_));
		stroke_.clear();
	}
}

void FieldEditorScene::undo()
{
	if(undoStack_.empty()){
		return;
	}
	for(auto it = undoStack_.back().rbegin(); it != undoStack_.back().rend(); ++it){
		map_.set(it->x, it->z, it->before);
	}
	undoStack_.pop_back();
	dirty_ = true;
}

void FieldEditorScene::save()
{
	if(map_.save(filePath(), tiles_)){
		SDL_Log("field: saved %s", filePath().c_str());
		++savedCount_;
		dirty_ = false;
	}
}

void FieldEditorScene::reload()
{
	if(map_.load(filePath(), tiles_)){
		SDL_Log("field: loaded %s", filePath().c_str());
	}
	undoStack_.clear();
	dirty_ = false;
}

void FieldEditorScene::command(const std::string &name, double value)
{
	if(name == "brush"){
		brush_ = std::clamp(static_cast<int>(value), 0, static_cast<int>(tiles_.size()) - 1);
	}
	else if(name == "save"){
		save();
	}
	else if(name == "load"){
		reload();
	}
	else if(name == "undo"){
		undo();
	}
	else if(name == "fill"){
		std::vector<Edit> edits;
		for(int z = 0; z < map_.depth(); ++z){
			for(int x = 0; x < map_.width(); ++x){
				if(map_.get(x, z) != brush_){
					edits.push_back({x, z, map_.get(x, z)});
				}
			}
		}
		map_.fill(static_cast<uint8_t>(brush_));
		if(!edits.empty()){
			undoStack_.push_back(std::move(edits));
			dirty_ = true;
		}
	}
	else{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "editor command: unknown: %s", name.c_str());
	}
}

void FieldEditorScene::dispatch(const SDL_Event &event)
{
	if(!ui_){
		return;
	}
	switch(event.type){
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		if(!event.key.repeat){ // Escなどのキーは、Luaのスクリプトが受ける(メニューの開閉。保存・読み直し・終了は、メニューから)
			ui_->onKey(SDL_GetKeyName(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
		}
		break;
	case SDL_EVENT_MOUSE_MOTION: {
		mouseX_ = event.motion.x;
		mouseY_ = event.motion.y;
		float x, y;
		toScreen(mouseX_, mouseY_, x, y);
		ui_->onMouseMove(x, y);
		if(rotating_){
			yaw_ -= event.motion.xrel * 0.005f;
			pitch_ = std::clamp(pitch_ + event.motion.yrel * 0.005f, 0.15f, 1.5f);
		}
		break;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
		float x, y;
		toScreen(event.button.x, event.button.y, x, y);
		ui_->onMouseButton(event.button.button, down, x, y);
		if(event.button.button == SDL_BUTTON_LEFT){
			if(down && !ui_->hasHover()){
				painting_ = true;
				int cx, cz;
				if(pickCell(event.button.x, event.button.y, cx, cz)){
					paint(cx, cz);
				}
			}
			else if(!down){
				endStroke();
			}
		}
		else if(event.button.button == SDL_BUTTON_RIGHT){
			rotating_ = down && !ui_->hasHover();
		}
		break;
	}
	case SDL_EVENT_MOUSE_WHEEL:
		distance_ = std::clamp(distance_ * std::exp(-event.wheel.y * 0.1f), 4.0f, 80.0f);
		break;
	default:
		break;
	}
}

void FieldEditorScene::drawField(const geo::Matrix4x4f &viewProj)
{
	auto &window = vulkanWindow(*this);
	const float cell = map_.cellSize();
	// 土台(マスの隙間が、格子線に見える)
	{
		geo::AffineMap base;
		base.setPos(geo::Vector3f(map_.width() * cell * 0.5f, -0.02f, map_.depth() * cell * 0.5f));
		base.setScale(geo::Vector3f(map_.width() * cell, 1.0f, map_.depth() * cell));
		window.draw(quad_, viewProj * base.getMatrix(), base.getMatrix(), baseMaterial_);
	}
	for(int z = 0; z < map_.depth(); ++z){
		for(int x = 0; x < map_.width(); ++x){
			geo::AffineMap tile;
			tile.setPos(geo::Vector3f((x + 0.5f) * cell, 0.0f, (z + 0.5f) * cell));
			tile.setScale(geo::Vector3f(cell * 0.96f, 1.0f, cell * 0.96f));
			window.draw(quad_, viewProj * tile.getMatrix(), tile.getMatrix(), tileMaterials_[std::min<size_t>(map_.get(x, z), tileMaterials_.size() - 1)]);
		}
	}
	// カーソルのあるマス(半透明の白い板)
	if(hoverX_ >= 0 && !ui_->hasHover()){
		geo::AffineMap mark;
		mark.setPos(geo::Vector3f((hoverX_ + 0.5f) * cell, 0.03f, (hoverZ_ + 0.5f) * cell));
		mark.setRotation(geo::Quaternionf::createRotater(-1.5707963f, geo::Vector3f(1.0f, 0.0f, 0.0f)));
		mark.setScale(geo::Vector3f(cell, cell, 1.0f));
		window.drawSprite3D(white_, viewProj * mark.getMatrix(), 1.0f, 1.0f, 1.0f, 0.4f);
	}
}

bool FieldEditorScene::onIdle(uint32_t tick)
{
	Scene::onIdle(tick);
	const bool running = true;
	if(isFinished() || !ui_){
		return running;
	}
	auto &window = vulkanWindow(*this);
	const float dt = std::min(static_cast<float>(tick - lastTick_) * 0.001f, 0.1f);
	lastTick_ = tick;

	// 動作確認用: VULKAN_AUTOPAINT="x,z,タイル番号@ミリ秒" で、そのマスを塗る(画面を撮って確認するため)。VULKAN_AUTOSAVE=1 で、塗った後に保存
	static const char *autoPaint = SDL_getenv("VULKAN_AUTOPAINT");
	if(autoPaint && !autoDone_ && static_cast<int>(tick - startTick_) >= 500){
		autoDone_ = true;
		int x0 = 0, z0 = 0, t = 0;
		if(std::sscanf(autoPaint, "%d,%d,%d", &x0, &z0, &t) == 3){
			brush_ = t;
			for(int i = 0; i < 6; ++i){
				for(int j = 0; j < 4; ++j){
					paint(x0 + i, z0 + j);
				}
			}
			endStroke();
			if(SDL_getenv("VULKAN_AUTOSAVE")){
				save();
			}
		}
	}

	// 動作確認用: VULKAN_AUTOKEY="キー名@ミリ秒,..."(時刻の昇順)で、シーン開始からその時間後に、そのキーが押されたことにする(LuaUiSceneと同じ書式)
	static const char *autoKey = SDL_getenv("VULKAN_AUTOKEY");
	if(autoKey){
		const std::string all(autoKey);
		std::vector<std::pair<std::string, int>> keys;
		for(size_t start = 0; start < all.size();){
			size_t end = all.find(',', start);
			end = end == std::string::npos ? all.size() : end;
			const std::string spec = all.substr(start, end - start);
			const auto at = spec.find('@');
			if(at != std::string::npos){
				keys.emplace_back(spec.substr(0, at), std::atoi(spec.c_str() + at + 1));
			}
			start = end + 1;
		}
		while(autoKeyDone_ < keys.size() && static_cast<int>(tick - startTick_) >= keys[autoKeyDone_].second){
			const std::string key = keys[autoKeyDone_++].first;
			ui_->onKey(key, true);
			ui_->onKey(key, false);
		}
	}

	// カメラの移動(W/A/S/D)・回転(Q/E)
	const bool *keys = SDL_GetKeyboardState(nullptr);
	const float move = (keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * 1.0f;
	const float forwardMove = (keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) * 1.0f;
	const float speed = distance_ * 0.8f * dt;
	targetX_ += (std::cos(yaw_) * move - std::sin(yaw_) * forwardMove) * speed;
	targetZ_ += (-std::sin(yaw_) * move - std::cos(yaw_) * forwardMove) * speed;
	targetX_ = std::clamp(targetX_, 0.0f, map_.width() * map_.cellSize());
	targetZ_ = std::clamp(targetZ_, 0.0f, map_.depth() * map_.cellSize());
	yaw_ += (keys[SDL_SCANCODE_Q] - keys[SDL_SCANCODE_E]) * 1.5f * dt;

	const geo::Vector3f target(targetX_, 0.0f, targetZ_);
	eye_ = target + geo::Vector3f(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)) * distance_;
	const auto view = geo::createLookAt<float>(eye_, target, {0.0f, 1.0f, 0.0f});
	const auto proj = vk_::createPerspective(fovY_, window.getScreenWidth(), window.getScreenHeight(), 0.1f, 300.0f);
	window.setCameraPosition(eye_);

	// カーソルのマス、ドラッグで塗る
	if(!pickCell(mouseX_, mouseY_, hoverX_, hoverZ_)){
		hoverX_ = hoverZ_ = -1;
	}
	if(painting_ && hoverX_ >= 0){
		paint(hoverX_, hoverZ_);
	}

	drawField(proj * view);

	// 画面の部品へ、状態を渡す
	auto &world = uiContext_->world();
	world.values["brush"] = static_cast<float>(brush_);
	world.values["cellX"] = static_cast<float>(hoverX_);
	world.values["cellZ"] = static_cast<float>(hoverZ_);
	world.values["dirty"] = dirty_ ? 1.0f : 0.0f;
	world.values["saved"] = static_cast<float>(savedCount_);
	world.values["width"] = static_cast<float>(map_.width());
	world.values["depth"] = static_cast<float>(map_.depth());
	world.fps = dt > 0.0f ? (world.fps <= 0.0f ? 1.0f / dt : world.fps + (1.0f / dt - world.fps) * 0.1f) : world.fps;
	padNavigator_.poll(getResources(), *ui_, tick);
	ui_->update(dt, static_cast<float>(tick - startTick_) * 0.001f);
	ui_->draw();
	swap();
	return running;
}
