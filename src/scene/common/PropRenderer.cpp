#include "scene/common/PropRenderer.h"
#include "field/PropCatalog.h"
#include "geo/AffineMap.h"
#include "scene/common/ModelFactory.h"
#include "sdl/SDLVulkanWindow.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace game
{

PropRenderer::PropRenderer(SDL_::VulkanWindow &window, ResourceSet &resources)
	: window_(window)
	, resources_(resources)
{
}

const PropRenderer::Entry &PropRenderer::entry(const std::string &name)
{
	const auto found = entries_.find(name);
	if(found != entries_.end()){
		return found->second;
	}
	Entry result;
	result.model = createVulkanModel(window_, resources_, field::propPath(name));
	if(result.model){
		// 静止した物として一度だけ姿勢を解く(ノードの階層は、読み込み時に頂点へ反映済みで、休止ポーズのまま)
		if(auto *skeleton = result.model->skeleton()){
			skeleton->resetPose();
		}
		result.model->updatePose();
		float radius = 0.0f, height = 0.0f;
		for(const auto &vertex : result.model->data().vertices){
			radius = std::max(radius, std::hypot(vertex.position[0], vertex.position[2]));
			height = std::max(height, vertex.position[1]);
		}
		result.radius = std::max(radius, 0.1f);
		result.height = height;
		result.footprint = field::computeFootprint(result.model->data());
	}
	else{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "prop: cannot load %s", name.c_str());
	}
	return entries_.emplace(name, std::move(result)).first->second;
}

float PropRenderer::radius(const std::string &name)
{
	return entry(name).radius;
}

void PropRenderer::drawOne(const std::string &name, float x, float y, float z, float yaw, float scale, const geo::Matrix4x4f &viewProj)
{
	drawEntry(entry(name), x, y, z, yaw, scale, viewProj);
}

void PropRenderer::drawEntry(const Entry &e, float x, float y, float z, float yaw, float scale, const geo::Matrix4x4f &viewProj)
{
	if(!e.model){
		return;
	}
	geo::AffineMap transform;
	transform.setPos(geo::Vector3f(x, y, z));
	transform.setRotation(geo::Quaternionf::createRotater(yaw, geo::Vector3f(0.0f, 1.0f, 0.0f)));
	transform.setScale(geo::Vector3f(scale, scale, scale));
	window_.draw(e.model, viewProj, transform.getMatrix());
}

void PropRenderer::draw(const field::FieldMap &map, const geo::Matrix4x4f &viewProj)
{
	// 番号→Entryの対応は、このフレームだけ持つ(エディタがマップを読み直すと、番号の指す名前が変わるため)。容量は使い回す
	resolved_.assign(map.propNameCount(), nullptr);
	for(const auto &prop : map.props()){
		const Entry *&e = resolved_[prop.prop];
		if(!e){
			e = &entry(map.propName(prop.prop));
		}
		drawEntry(*e, prop.x, map.propBaseY(prop), prop.z, prop.yaw, prop.scale, viewProj);
	}
}

} // namespace game
