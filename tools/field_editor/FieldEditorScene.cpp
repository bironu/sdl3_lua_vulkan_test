#include "app/Application.h"
#include "FieldEditorScene.h"
#include "field/PropCatalog.h"
#include "geo/AffineMap.h"
#include "geo/Calculator.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "resources/Resources.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include "ui/PadNames.h"
#include "vk/PrimitiveMeshes.h"
#include "vk/Vertex.h"
#include "vk/VulkanMath.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
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
	white_ = window.createTexture(solidImage(255, 255, 255));
	slopeMaterial_.texture = white_;
	slopeMaterial_.specular = 0.0f;
	slopeMaterial_.castShadow = false;
	slopeMaterial_.receiveShadow = false;
	slopeMaterial_.alpha = 0.5f;
	propRenderer_ = std::make_unique<game::PropRenderer>(window, resources());
	refreshProps();

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

void FieldEditorScene::refreshProps()
{
	const std::string selected = selectedProp();
	propList_ = field::listProps();
	propSel_ = -1;
	for(size_t i = 0; i < propList_.size(); ++i){
		if(propList_[i] == selected){
			propSel_ = static_cast<int>(i);
		}
	}
	if(propSel_ < 0 && !propList_.empty()){
		propSel_ = 0;
	}
}

std::string FieldEditorScene::selectedProp() const
{
	return propSel_ >= 0 && propSel_ < static_cast<int>(propList_.size()) ? propList_[static_cast<size_t>(propSel_)] : std::string();
}

void FieldEditorScene::importFile(const std::string &path)
{
	std::string name;
	if(!field::importProp(path, name)){
		return;
	}
	SDL_Log("field: imported prop %s", name.c_str());
	refreshProps();
	for(size_t i = 0; i < propList_.size(); ++i){
		if(propList_[i] == name){
			propSel_ = static_cast<int>(i);
			propPage_ = propSel_ / kPropsPerPage;
		}
	}
	mode_ = ModeProp;
	++importedCount_;
}

void FieldEditorScene::toScreen(float windowX, float windowY, float &x, float &y)
{
	const auto size = getWindow().getSize();
	x = windowX / static_cast<float>(std::max(size.getX(), 1)) * uiContext_->screenWidth();
	y = windowY / static_cast<float>(std::max(size.getY(), 1)) * uiContext_->screenHeight();
}

bool FieldEditorScene::pickGround(float windowX, float windowY, float hit[3]) const
{
	const auto size = const_cast<FieldEditorScene *>(this)->getWindow().getSize();
	const float nx = windowX / static_cast<float>(std::max(size.getX(), 1)) * 2.0f - 1.0f;
	const float ny = 1.0f - windowY / static_cast<float>(std::max(size.getY(), 1)) * 2.0f; // 上が+
	const float aspect = static_cast<float>(size.getX()) / static_cast<float>(std::max(size.getY(), 1));
	// カメラの座標系(eye → target を前、世界の上を+Yとして、右・上を求める)
	const geo::Vector3f target(targetX_, targetY_, targetZ_);
	const geo::Vector3f forward = geo::Vector3f::normalize(target - eye_);
	const geo::Vector3f right = geo::Vector3f::normalize(geo::Vector3f::cross(forward, geo::Vector3f(0.0f, 1.0f, 0.0f)));
	const geo::Vector3f up = geo::Vector3f::cross(right, forward);
	const float tanHalf = std::tan(fovY_ * 0.5f);
	const geo::Vector3f dir = geo::Vector3f::normalize(forward + right * (nx * tanHalf * aspect) + up * (ny * tanHalf));
	const float origin[3] = {eye_.getX(), eye_.getY(), eye_.getZ()};
	const float direction[3] = {dir.getX(), dir.getY(), dir.getZ()};
	float t = 0.0f;
	return map_.raycast(origin, direction, distance_ * 4.0f + 200.0f, t, hit);
}

void FieldEditorScene::beginStroke()
{
	painting_ = true;
	stroke_ = Action();
	touchedHeights_.clear();
	if(hasHit_){
		flattenHeight_ = map_.heightAt(hit_[0], hit_[2]);
		if(mode_ == ModeHeight && tool_ == ToolRamp){
			rampActive_ = true;
			rampStart_[0] = rampEnd_[0] = hit_[0];
			rampStart_[1] = rampEnd_[1] = hit_[2];
		}
	}
}

void FieldEditorScene::endStroke()
{
	painting_ = false;
	if(rampActive_){
		applyRamp(); // 斜面ツール: ドラッグを離したときに、始点から終点へつなぐ
		rampActive_ = false;
	}
	commit(std::move(stroke_));
	stroke_ = Action();
	touchedHeights_.clear();
}

void FieldEditorScene::commit(Action &&action)
{
	if(!action.empty()){
		undoStack_.push_back(std::move(action));
		dirty_ = true;
	}
}

void FieldEditorScene::paintTiles(float x, float z)
{
	const float cell = map_.cellSize();
	const int cx = static_cast<int>(std::floor(x / cell)), cz = static_cast<int>(std::floor(z / cell));
	for(int dz = -radius_; dz <= radius_; ++dz){
		for(int dx = -radius_; dx <= radius_; ++dx){
			const int tx = cx + dx, tz = cz + dz;
			if(!map_.inside(tx, tz)){
				continue;
			}
			if(radius_ > 0){
				const float ex = (tx + 0.5f) * cell - x, ez = (tz + 0.5f) * cell - z;
				if(std::sqrt(ex * ex + ez * ez) > radius_ * cell + 0.01f){
					continue;
				}
			}
			const uint8_t before = map_.get(tx, tz);
			if(map_.set(tx, tz, static_cast<uint8_t>(brush_))){
				stroke_.tiles.push_back({tz * map_.width() + tx, before});
				renderer_->invalidate(tx, tz, tx, tz);
				slopeDirty_ = true; // 歩けないタイルの表示が変わる
			}
		}
	}
}

void FieldEditorScene::sculpt(float x, float z, float dt)
{
	const float cell = map_.cellSize();
	const float reach = static_cast<float>(radius_ + 1);
	const int cx = static_cast<int>(std::round(x / cell)), cz = static_cast<int>(std::round(z / cell));
	const bool lowerKey = Application::getKeybordState()[SDL_SCANCODE_LSHIFT] || Application::getKeybordState()[SDL_SCANCODE_RSHIFT];
	int tool = tool_;
	if(lowerKey && tool == ToolRaise){
		tool = ToolLower;
	}
	else if(lowerKey && tool == ToolLower){
		tool = ToolRaise;
	}
	const int span = radius_ + 2;
	// ならすときは、変える前の高さから平均を求める(順番に依存しないように、先に新しい値を決める)
	struct Change { int vx, vz; float height; };
	std::vector<Change> changes;
	for(int vz = cz - span; vz <= cz + span; ++vz){
		for(int vx = cx - span; vx <= cx + span; ++vx){
			if(!map_.insideVertex(vx, vz)){
				continue;
			}
			const float ex = vx * cell - x, ez = vz * cell - z;
			const float distance = std::sqrt(ex * ex + ez * ez) / cell;
			if(distance > reach){
				continue;
			}
			const float falloff = 1.0f - distance / reach;
			const float weight = falloff * falloff * (3.0f - 2.0f * falloff);
			const float height = map_.vertexHeight(vx, vz);
			float target = height;
			switch(tool){
			case ToolRaise:
				target = height + 6.0f * dt * weight;
				break;
			case ToolLower:
				target = height - 6.0f * dt * weight;
				break;
			case ToolSmooth: {
				const float average = (height + map_.vertexHeight(vx - 1, vz) + map_.vertexHeight(vx + 1, vz) + map_.vertexHeight(vx, vz - 1) + map_.vertexHeight(vx, vz + 1)) / 5.0f;
				target = height + (average - height) * std::min(1.0f, 10.0f * dt * weight);
				break;
			}
			case ToolFlatten:
				target = height + (flattenHeight_ - height) * std::min(1.0f, 12.0f * dt * weight);
				break;
			}
			changes.push_back({vx, vz, std::clamp(target, -60.0f, 60.0f)});
		}
	}
	for(const auto &change : changes){
		const int index = map_.vertexIndex(change.vx, change.vz);
		const int16_t before = map_.rawHeightAt(index);
		if(map_.setVertexHeight(change.vx, change.vz, change.height)){
			if(touchedHeights_.insert(index).second){
				stroke_.heights.push_back({index, before});
			}
			renderer_->invalidate(change.vx - 1, change.vz - 1, change.vx, change.vz);
			slopeDirty_ = true;
		}
	}
}

void FieldEditorScene::placeProp(float x, float z)
{
	const std::string name = selectedProp();
	if(name.empty() || !map_.inside(static_cast<int>(std::floor(x / map_.cellSize())), static_cast<int>(std::floor(z / map_.cellSize())))){
		return;
	}
	Action action;
	const size_t index = map_.addProp(name, x, z, propYaw_, propScale_, propLift_);
	action.props.push_back({true, index, map_.props()[index]});
	commit(std::move(action));
}

void FieldEditorScene::updateHoverProp()
{
	hoverProp_ = -1;
	if(mode_ != ModeProp || !hasHit_){
		return;
	}
	float best = 1e9f;
	for(size_t i = 0; i < map_.props().size(); ++i){
		const auto &prop = map_.props()[i];
		const float dx = prop.x - hit_[0], dz = prop.z - hit_[2];
		const float distance = std::sqrt(dx * dx + dz * dz);
		const float reach = std::max(propRenderer_->radius(map_.propName(prop.prop)) * prop.scale * 0.7f, 0.6f);
		if(distance <= reach && distance < best){
			best = distance;
			hoverProp_ = static_cast<int>(i);
		}
	}
}

void FieldEditorScene::removeHoveredProp()
{
	if(hoverProp_ < 0 || hoverProp_ >= static_cast<int>(map_.props().size())){
		return;
	}
	Action action;
	action.props.push_back({false, static_cast<size_t>(hoverProp_), map_.removeProp(static_cast<size_t>(hoverProp_))});
	commit(std::move(action));
	hoverProp_ = -1;
}

void FieldEditorScene::undo()
{
	if(undoStack_.empty()){
		return;
	}
	Action action = std::move(undoStack_.back());
	undoStack_.pop_back();
	for(auto it = action.props.rbegin(); it != action.props.rend(); ++it){
		if(it->added){
			map_.removeProp(it->index);
		}
		else{
			map_.insertProp(it->index, it->prop);
		}
	}
	for(auto it = action.heights.rbegin(); it != action.heights.rend(); ++it){
		map_.setRawHeight(it->index, it->before);
		const int vx = it->index % map_.vertexCountX(), vz = it->index / map_.vertexCountX();
		renderer_->invalidate(vx - 1, vz - 1, vx, vz);
	}
	for(auto it = action.tiles.rbegin(); it != action.tiles.rend(); ++it){
		const int x = it->index % map_.width(), z = it->index / map_.width();
		map_.set(x, z, it->before);
		renderer_->invalidate(x, z, x, z);
	}
	slopeDirty_ = true;
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

void FieldEditorScene::rebuildField()
{
	renderer_ = std::make_unique<game::FieldRenderer>(vulkanWindow(*this), map_, tiles_);
}

void FieldEditorScene::reload()
{
	if(map_.load(filePath(), tiles_)){
		SDL_Log("field: loaded %s (%dx%d, %zu props)", filePath().c_str(), map_.width(), map_.depth(), map_.props().size());
	}
	rebuildField();
	slopeDirty_ = true;
	undoStack_.clear();
	dirty_ = false;
}

void FieldEditorScene::command(const std::string &name, double value)
{
	if(name == "mode"){
		mode_ = std::clamp(static_cast<int>(value), 0, 2);
	}
	else if(name == "brush"){
		brush_ = std::clamp(static_cast<int>(value), 0, static_cast<int>(tiles_.size()) - 1);
	}
	else if(name == "tool"){
		tool_ = std::clamp(static_cast<int>(value), 0, static_cast<int>(ToolCount) - 1);
	}
	else if(name == "radius"){
		radius_ = std::clamp(static_cast<int>(value), 0, 12);
	}
	else if(name == "prop"){ // ページの中の何番目か
		const int index = propPage_ * kPropsPerPage + static_cast<int>(value);
		if(index >= 0 && index < static_cast<int>(propList_.size())){
			propSel_ = index;
		}
	}
	else if(name == "page"){
		const int pages = std::max(1, (static_cast<int>(propList_.size()) + kPropsPerPage - 1) / kPropsPerPage);
		propPage_ = std::clamp(propPage_ + static_cast<int>(value), 0, pages - 1);
	}
	else if(name == "yaw"){
		propYaw_ = std::fmod(propYaw_ + static_cast<float>(value) * 0.0174532925f, 6.2831853f);
	}
	else if(name == "scale"){
		propScale_ = std::clamp(propScale_ * static_cast<float>(value), 0.1f, 20.0f);
	}
	else if(name == "lift"){
		propLift_ = std::clamp(propLift_ + static_cast<float>(value), -20.0f, 50.0f);
	}
	else if(name == "slopes"){
		showSlopes_ = !showSlopes_;
		slopeDirty_ = true;
	}
	else if(name == "collision"){
		showCollision_ = !showCollision_;
	}
	else if(name == "delete"){
		removeHoveredProp();
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
	else if(name == "fill"){ // モードのとおり: タイルなら全面を塗る、高さなら全面を平らにする(いまの高さ0へ)
		Action action;
		if(mode_ == ModeHeight){
			for(int i = 0; i < map_.vertexCountX() * map_.vertexCountZ(); ++i){
				if(map_.rawHeightAt(i) != 0){
					action.heights.push_back({i, map_.rawHeightAt(i)});
					map_.setRawHeight(i, 0);
				}
			}
		}
		else{
			for(int z = 0; z < map_.depth(); ++z){
				for(int x = 0; x < map_.width(); ++x){
					if(map_.get(x, z) != brush_){
						action.tiles.push_back({z * map_.width() + x, map_.get(x, z)});
					}
				}
			}
			map_.fill(static_cast<uint8_t>(brush_));
		}
		if(!action.empty()){
			commit(std::move(action));
			slopeDirty_ = true;
			renderer_->invalidate(0, 0, map_.width(), map_.depth());
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
		if(!event.key.repeat || event.key.key == SDLK_PAGEUP || event.key.key == SDLK_PAGEDOWN){ // Escなどのキーは、Luaのスクリプトが受ける(メニューの開閉。保存・読み直し・終了は、メニューから)
			ui_->onKey(SDL_GetKeyName(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
		}
		break;
	case SDL_EVENT_DROP_FILE:
		if(event.drop.data){
			importFile(event.drop.data);
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
				hasHit_ = pickGround(event.button.x, event.button.y, hit_);
				if(mode_ == ModeProp){
					if(hasHit_){
						placeProp(hit_[0], hit_[2]);
					}
				}
				else{
					beginStroke(); // 塗り・彫りは、onIdleで毎フレーム続ける
				}
			}
			else if(!down && painting_){
				endStroke();
			}
		}
		else if(event.button.button == SDL_BUTTON_RIGHT){
			rotating_ = down && !ui_->hasHover();
		}
		break;
	}
	case SDL_EVENT_MOUSE_WHEEL:
		distance_ = std::clamp(distance_ * std::exp(-event.wheel.y * 0.1f), 4.0f, 120.0f);
		break;
	default:
		break;
	}
}

int FieldEditorScene::slopeClass(float slope) const
{
	if(slope <= settings_.maxSlope){
		return 0;
	}
	return slope <= settings_.maxClimbSlope ? 1 : 2;
}

// 勾配の表示: マスごとに、その2枚の三角形の勾配(高さの差/距離)の大きい方で、色を決める。歩けないタイルは紫
void FieldEditorScene::rebuildSlopeMesh()
{
	slopeDirty_ = false;
	std::vector<vk_::Vertex> vertices;
	std::vector<uint32_t> indices;
	const float cell = map_.cellSize();
	static const float kColors[4][3] = {{0.2f, 0.9f, 0.3f}, {1.0f, 0.85f, 0.1f}, {1.0f, 0.2f, 0.2f}, {0.65f, 0.25f, 0.95f}};
	for(int z = 0; z < map_.depth(); ++z){
		for(int x = 0; x < map_.width(); ++x){
			const float h00 = map_.vertexHeight(x, z), h10 = map_.vertexHeight(x + 1, z), h01 = map_.vertexHeight(x, z + 1), h11 = map_.vertexHeight(x + 1, z + 1);
			const float g1 = std::hypot(h10 - h00, h11 - h10) / cell, g2 = std::hypot(h11 - h01, h01 - h00) / cell;
			const size_t tile = map_.get(x, z);
			const bool blocked = tile < tiles_.size() && !tiles_[tile].walkable;
			const float *color = kColors[blocked ? 3 : slopeClass(std::max(g1, g2))];
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			const int vx[4] = {x, x, x + 1, x + 1}, vz[4] = {z, z + 1, z + 1, z};
			for(int k = 0; k < 4; ++k){
				vk_::Vertex vertex{};
				vertex.position[0] = vx[k] * cell;
				vertex.position[1] = map_.vertexHeight(vx[k], vz[k]) + 0.04f;
				vertex.position[2] = vz[k] * cell;
				vertex.color[0] = color[0];
				vertex.color[1] = color[1];
				vertex.color[2] = color[2];
				map_.vertexNormal(vx[k], vz[k], vertex.normal);
				vertices.push_back(vertex);
			}
			for(uint32_t i : {0u, 1u, 2u, 2u, 3u, 0u}){
				indices.push_back(base + i);
			}
		}
	}
	slopeMesh_ = std::make_shared<VulkanMesh>(vulkanWindow(*this).getContext(), vertices.data(), static_cast<uint32_t>(vertices.size()),
		indices.data(), static_cast<uint32_t>(indices.size()));
}

// 斜面ツール: 始点の高さから終点の高さへ、一定の勾配でつなぐ。始点→終点の線から、ブラシの半径(幅)の中の頂点を、その線上の高さへ寄せる(幅の外へ1マスかけて、なじませる)
void FieldEditorScene::applyRamp()
{
	const float cell = map_.cellSize();
	const float ax = rampStart_[0], az = rampStart_[1], bx = rampEnd_[0], bz = rampEnd_[1];
	const float dx = bx - ax, dz = bz - az;
	const float lengthSquared = dx * dx + dz * dz;
	if(lengthSquared < cell * cell){
		return; // 短すぎる(ほとんど動かさなかった)
	}
	const float ha = map_.heightAt(ax, az), hb = map_.heightAt(bx, bz);
	const float halfWidth = (static_cast<float>(radius_) + 0.5f) * cell;
	const float reach = halfWidth + cell;
	const int vx0 = std::max(0, static_cast<int>(std::floor((std::min(ax, bx) - reach) / cell))), vx1 = std::min(map_.width(), static_cast<int>(std::ceil((std::max(ax, bx) + reach) / cell)));
	const int vz0 = std::max(0, static_cast<int>(std::floor((std::min(az, bz) - reach) / cell))), vz1 = std::min(map_.depth(), static_cast<int>(std::ceil((std::max(az, bz) + reach) / cell)));
	struct Change { int vx, vz; float height; };
	std::vector<Change> changes;
	for(int vz = vz0; vz <= vz1; ++vz){
		for(int vx = vx0; vx <= vx1; ++vx){
			const float px = vx * cell - ax, pz = vz * cell - az;
			const float t = std::clamp((px * dx + pz * dz) / lengthSquared, 0.0f, 1.0f);
			const float ex = px - dx * t, ez = pz - dz * t;
			const float distance = std::sqrt(ex * ex + ez * ez);
			if(distance > reach){
				continue;
			}
			const float weight = distance <= halfWidth ? 1.0f : 1.0f - (distance - halfWidth) / cell;
			const float height = map_.vertexHeight(vx, vz);
			const float target = ha + (hb - ha) * t;
			changes.push_back({vx, vz, std::clamp(height + (target - height) * weight, -60.0f, 60.0f)});
		}
	}
	for(const auto &change : changes){
		const int index = map_.vertexIndex(change.vx, change.vz);
		const int16_t before = map_.rawHeightAt(index);
		if(map_.setVertexHeight(change.vx, change.vz, change.height)){
			if(touchedHeights_.insert(index).second){
				stroke_.heights.push_back({index, before});
			}
			renderer_->invalidate(change.vx - 1, change.vz - 1, change.vx, change.vz);
		}
	}
	slopeDirty_ = true;
}

// 斜面ツールの、ドラッグ中の見本: 始点から終点への線を、できる勾配の色(緑・黄・赤)の点で
void FieldEditorScene::drawRampPreview(const geo::Matrix4x4f &viewProj)
{
	auto &window = vulkanWindow(*this);
	const float cell = map_.cellSize();
	const float dx = rampEnd_[0] - rampStart_[0], dz = rampEnd_[1] - rampStart_[1];
	const float length = std::sqrt(dx * dx + dz * dz);
	if(length < 1e-3f){
		return;
	}
	const float ha = map_.heightAt(rampStart_[0], rampStart_[1]), hb = map_.heightAt(rampEnd_[0], rampEnd_[1]);
	const int klass = slopeClass(std::fabs(hb - ha) / length);
	static const float kColors[3][3] = {{0.2f, 1.0f, 0.3f}, {1.0f, 0.9f, 0.1f}, {1.0f, 0.25f, 0.25f}};
	const int steps = std::max(2, static_cast<int>(length / (cell * 0.5f)));
	for(int i = 0; i <= steps; ++i){
		const float t = static_cast<float>(i) / steps;
		const float x = rampStart_[0] + dx * t, z = rampStart_[1] + dz * t;
		geo::AffineMap mark;
		mark.setPos(geo::Vector3f(x, map_.heightAt(x, z) + 0.15f, z));
		mark.setRotation(geo::Quaternionf::createRotater(-1.5707963f, geo::Vector3f(1.0f, 0.0f, 0.0f)));
		mark.setScale(geo::Vector3f(cell * 0.4f, cell * 0.4f, 1.0f));
		window.drawSprite3D(white_, viewProj * mark.getMatrix(), kColors[klass][0], kColors[klass][1], kColors[klass][2], 0.9f);
	}
}

void FieldEditorScene::drawField(const geo::Matrix4x4f &viewProj)
{
	auto &window = vulkanWindow(*this);
	renderer_->update(map_);
	renderer_->draw(window, viewProj);
	propRenderer_->draw(map_, viewProj);
	if(showSlopes_){
		if(slopeDirty_ || !slopeMesh_){
			rebuildSlopeMesh();
		}
		window.draw(slopeMesh_, viewProj, geo::createIdentityMatrix4x4<float>(), slopeMaterial_);
	}
	if(rampActive_){
		drawRampPreview(viewProj);
	}
	if(showCollision_){ // 置物の足元の当たり(ゲームで歩いて当たる範囲)の長方形
		for(const auto &prop : map_.props()){
			const auto &shape = propRenderer_->footprint(map_.propName(prop.prop));
			if(!shape.valid){
				continue;
			}
			geo::AffineMap mark;
			mark.setPos(geo::Vector3f(prop.x, map_.propBaseY(prop) + propRenderer_->height(map_.propName(prop.prop)) * prop.scale + 0.05f, prop.z)); // 地面では、物の中に隠れる。物のてっぺんへ投影して見せる
			mark.setRotation(geo::Quaternionf::createRotater(prop.yaw, geo::Vector3f(0.0f, 1.0f, 0.0f)) * geo::Quaternionf::createRotater(-1.5707963f, geo::Vector3f(1.0f, 0.0f, 0.0f)));
			mark.setScale(geo::Vector3f(1.0f, 1.0f, 1.0f));
			// 長方形の中心へずらした板(置いた向きで回る)
			geo::AffineMap center;
			center.setPos(geo::Vector3f((shape.minX + shape.maxX) * 0.5f * prop.scale, -(shape.minZ + shape.maxZ) * 0.5f * prop.scale, 0.0f)); // 板のyは、寝かせるとワールドの-Zになる
			center.setScale(geo::Vector3f((shape.maxX - shape.minX) * prop.scale, (shape.maxZ - shape.minZ) * prop.scale, 1.0f));
			window.drawSprite3D(white_, viewProj * mark.getMatrix() * center.getMatrix(), 1.0f, 0.6f, 0.1f, 0.5f);
		}
	}
	if(!hasHit_ || ui_->hasHover()){
		return;
	}
	const float cell = map_.cellSize();
	if(mode_ == ModeProp){
		if(hoverProp_ >= 0){ // 消す対象: 足元に赤い印
			const auto &prop = map_.props()[static_cast<size_t>(hoverProp_)];
			const float size = std::max(propRenderer_->radius(map_.propName(prop.prop)) * prop.scale * 2.0f, 1.0f);
			geo::AffineMap mark;
			mark.setPos(geo::Vector3f(prop.x, map_.propBaseY(prop) + 0.05f, prop.z));
			mark.setRotation(geo::Quaternionf::createRotater(-1.5707963f, geo::Vector3f(1.0f, 0.0f, 0.0f)));
			mark.setScale(geo::Vector3f(size, size, 1.0f));
			window.drawSprite3D(white_, viewProj * mark.getMatrix(), 1.0f, 0.2f, 0.2f, 0.45f);
		}
		else if(!selectedProp().empty()){ // 置く前の見本
			propRenderer_->drawOne(selectedProp(), hit_[0], map_.heightAt(hit_[0], hit_[2]) + propLift_, hit_[2], propYaw_, propScale_, viewProj);
		}
		return;
	}
	// ブラシの範囲(半透明の白い板。高さは、マスの角の高い方)
	const int cx = static_cast<int>(std::floor(hit_[0] / cell)), cz = static_cast<int>(std::floor(hit_[2] / cell));
	const int span = mode_ == ModeHeight ? radius_ + 1 : radius_;
	for(int dz = -span; dz <= span; ++dz){
		for(int dx = -span; dx <= span; ++dx){
			const int x = cx + dx, z = cz + dz;
			if(!map_.inside(x, z)){
				continue;
			}
			const float ex = (x + 0.5f) * cell - hit_[0], ez = (z + 0.5f) * cell - hit_[2];
			const float distance = std::sqrt(ex * ex + ez * ez);
			if(radius_ > 0 || mode_ == ModeHeight){
				if(distance > (mode_ == ModeHeight ? radius_ + 1 : radius_) * cell + 0.01f){
					continue;
				}
			}
			else if(dx != 0 || dz != 0){
				continue;
			}
			const float top = std::max({map_.vertexHeight(x, z), map_.vertexHeight(x + 1, z), map_.vertexHeight(x, z + 1), map_.vertexHeight(x + 1, z + 1)});
			geo::AffineMap mark;
			mark.setPos(geo::Vector3f((x + 0.5f) * cell, top + 0.04f, (z + 0.5f) * cell));
			mark.setRotation(geo::Quaternionf::createRotater(-1.5707963f, geo::Vector3f(1.0f, 0.0f, 0.0f)));
			mark.setScale(geo::Vector3f(cell, cell, 1.0f));
			window.drawSprite3D(white_, viewProj * mark.getMatrix(), 1.0f, 1.0f, 1.0f, mode_ == ModeHeight ? 0.18f : 0.35f);
		}
	}
}

// 動作確認用: "命令,値,...;命令,値,..." の1つ
void FieldEditorScene::runAuto(const std::string &spec)
{
	std::vector<std::string> parts;
	std::stringstream stream(spec);
	for(std::string part; std::getline(stream, part, ',');){
		parts.push_back(part);
	}
	if(parts.empty()){
		return;
	}
	auto number = [&](size_t i){ return i < parts.size() ? std::atof(parts[i].c_str()) : 0.0; };
	const std::string &name = parts[0];
	if(name == "stroke"){ // stroke,x,z,フレーム数: そのワールド座標を、いまのモード・道具で、ドラッグし続けたことにする
		hit_[0] = static_cast<float>(number(1));
		hit_[2] = static_cast<float>(number(2));
		hit_[1] = map_.heightAt(hit_[0], hit_[2]);
		hasHit_ = true;
		if(mode_ == ModeProp){
			placeProp(hit_[0], hit_[2]);
		}
		else{
			beginStroke();
			for(int i = 0; i < std::max(1, static_cast<int>(number(3))); ++i){
				if(mode_ == ModeTile){
					paintTiles(hit_[0], hit_[2]);
				}
				else{
					sculpt(hit_[0], hit_[2], 1.0f / 60.0f);
				}
			}
			endStroke();
		}
		hasHit_ = false;
	}
	else if(name == "ramp"){ // ramp,x0,z0,x1,z1: 斜面ツールで、(x0,z0)から(x1,z1)へドラッグしたことにする
		mode_ = ModeHeight;
		tool_ = ToolRamp;
		hit_[0] = static_cast<float>(number(1));
		hit_[2] = static_cast<float>(number(2));
		hasHit_ = true;
		beginStroke();
		rampEnd_[0] = static_cast<float>(number(3));
		rampEnd_[1] = static_cast<float>(number(4));
		endStroke();
		hasHit_ = false;
	}
	else if(name == "cam"){ // cam,yaw,pitch,距離,x,z
		yaw_ = static_cast<float>(number(1));
		pitch_ = static_cast<float>(number(2));
		distance_ = static_cast<float>(number(3));
		targetX_ = static_cast<float>(number(4));
		targetZ_ = static_cast<float>(number(5));
	}
	else if(name == "import"){
		importFile(spec.substr(spec.find(',') + 1));
	}
	else{
		command(name, number(1));
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

	// 動作確認用: VULKAN_AUTOCMD="命令,値,...@ミリ秒;..."(時刻の昇順)で、シーン開始からその時間後に、その編集をする(画面を撮って確認するため)。
	//   命令: mode,N / tool,N / brush,N / radius,N / prop,N / yaw,度 / scale,倍 / save / stroke,x,z,フレーム数 / ramp,x0,z0,x1,z1 / slopes / cam,yaw,pitch,距離,x,z / import,パス / undo
	static const char *autoCmd = SDL_getenv("VULKAN_AUTOCMD");
	if(autoCmd){
		const std::string all(autoCmd);
		std::vector<std::pair<std::string, int>> commands;
		for(size_t start = 0; start < all.size();){
			size_t end = all.find(';', start);
			end = end == std::string::npos ? all.size() : end;
			const std::string spec = all.substr(start, end - start);
			const auto at = spec.rfind('@');
			if(at != std::string::npos){
				commands.emplace_back(spec.substr(0, at), std::atoi(spec.c_str() + at + 1));
			}
			start = end + 1;
		}
		while(autoDone_ < commands.size() && static_cast<int>(tick - startTick_) >= commands[autoDone_].second){
			runAuto(commands[autoDone_++].first);
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
	const bool *keys = Application::getKeybordState();
	const float move = (keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * 1.0f;
	const float forwardMove = (keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) * 1.0f;
	const float speed = distance_ * 0.8f * dt;
	targetX_ += (std::cos(yaw_) * move - std::sin(yaw_) * forwardMove) * speed;
	targetZ_ += (-std::sin(yaw_) * move - std::cos(yaw_) * forwardMove) * speed;
	targetX_ = std::clamp(targetX_, 0.0f, map_.width() * map_.cellSize());
	targetZ_ = std::clamp(targetZ_, 0.0f, map_.depth() * map_.cellSize());
	yaw_ += (keys[SDL_SCANCODE_Q] - keys[SDL_SCANCODE_E]) * 1.5f * dt;
	// 注視点の高さは、地面に付いていく(急に変わらないよう、なめらかに)
	const float groundY = map_.heightAt(targetX_, targetZ_);
	targetY_ += (groundY - targetY_) * std::min(1.0f, 8.0f * dt);

	const geo::Vector3f target(targetX_, targetY_, targetZ_);
	eye_ = target + geo::Vector3f(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)) * distance_;
	const auto view = geo::createLookAt<float>(eye_, target, {0.0f, 1.0f, 0.0f});
	const auto proj = vk_::createPerspective(fovY_, window.getScreenWidth(), window.getScreenHeight(), 0.1f, 400.0f);
	window.setCameraPosition(eye_);

	// カーソルの指す地面。ドラッグ中は、塗り・彫りを続ける
	hasHit_ = !ui_->hasHover() && !rotating_ && pickGround(mouseX_, mouseY_, hit_);
	updateHoverProp();
	if(painting_ && hasHit_){
		if(mode_ == ModeTile){
			paintTiles(hit_[0], hit_[2]);
		}
		else if(mode_ == ModeHeight){
			if(tool_ == ToolRamp){
				rampEnd_[0] = hit_[0];
				rampEnd_[1] = hit_[2];
			}
			else{
				sculpt(hit_[0], hit_[2], dt);
			}
		}
	}

	drawField(proj * view);

	// 画面の部品へ、状態を渡す
	auto &world = uiContext_->world();
	world.values["mode"] = static_cast<float>(mode_);
	world.values["brush"] = static_cast<float>(brush_);
	world.values["tool"] = static_cast<float>(tool_);
	world.values["radius"] = static_cast<float>(radius_);
	world.values["propSel"] = static_cast<float>(propSel_ - propPage_ * kPropsPerPage); // ページの中の何番目か(ページの外ならはみ出す)
	world.values["propPage"] = static_cast<float>(propPage_);
	world.values["propPages"] = static_cast<float>(std::max(1, (static_cast<int>(propList_.size()) + kPropsPerPage - 1) / kPropsPerPage));
	world.values["slopes"] = showSlopes_ ? 1.0f : 0.0f;
	{
		const float dx = rampEnd_[0] - rampStart_[0], dz = rampEnd_[1] - rampStart_[1];
		const float length = std::sqrt(dx * dx + dz * dz);
		world.values["rampSlope"] = rampActive_ && length > 1e-3f ? (map_.heightAt(rampEnd_[0], rampEnd_[1]) - map_.heightAt(rampStart_[0], rampStart_[1])) / length : 0.0f;
	}
	world.values["propYaw"] = propYaw_ * 57.2957795f;
	world.values["propScale"] = propScale_;
	world.values["propLift"] = propLift_;
	world.values["propCount"] = static_cast<float>(map_.props().size());
	world.values["cellX"] = hasHit_ ? std::floor(hit_[0] / map_.cellSize()) : -1.0f;
	world.values["cellZ"] = hasHit_ ? std::floor(hit_[2] / map_.cellSize()) : -1.0f;
	world.values["hoverHeight"] = hasHit_ ? hit_[1] : 0.0f;
	world.values["dirty"] = dirty_ ? 1.0f : 0.0f;
	world.values["saved"] = static_cast<float>(savedCount_);
	world.values["imported"] = static_cast<float>(importedCount_);
	world.values["width"] = static_cast<float>(map_.width());
	world.values["depth"] = static_cast<float>(map_.depth());
	for(int i = 0; i < kPropsPerPage; ++i){
		const size_t index = static_cast<size_t>(propPage_ * kPropsPerPage + i);
		std::string label = index < propList_.size() ? propList_[index] : std::string();
		const auto dot = label.rfind('.');
		if(dot != std::string::npos){
			label.erase(dot);
		}
		world.strings["prop" + std::to_string(i + 1)] = label;
	}
	world.fps = dt > 0.0f ? (world.fps <= 0.0f ? 1.0f / dt : world.fps + (1.0f / dt - world.fps) * 0.1f) : world.fps;
	padNavigator_.poll(getResources(), *ui_, tick);
	ui_->update(dt, static_cast<float>(tick - startTick_) * 0.001f);
	ui_->draw();
	swap();
	return running;
}
