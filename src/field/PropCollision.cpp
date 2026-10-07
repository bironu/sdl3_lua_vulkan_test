#include "field/PropCollision.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace field
{

PropFootprint computeFootprint(const model::ModelData &data, float sliceTop)
{
	PropFootprint result;
	float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
	auto add = [&](const float p[3]){
		minX = std::min(minX, p[0]);
		maxX = std::max(maxX, p[0]);
		minZ = std::min(minZ, p[2]);
		maxZ = std::max(maxZ, p[2]);
		result.valid = true;
	};
	for(size_t i = 0; i + 2 < data.indices.size(); i += 3){
		const float *corner[3] = {data.vertices[data.indices[i]].position, data.vertices[data.indices[i + 1]].position, data.vertices[data.indices[i + 2]].position};
		// 三角形を y <= sliceTop の側へ切って、残った部分の頂点の広がりを取る
		for(int k = 0; k < 3; ++k){
			const float *a = corner[k], *b = corner[(k + 1) % 3];
			const bool inA = a[1] <= sliceTop, inB = b[1] <= sliceTop;
			if(inA){
				add(a);
			}
			if(inA != inB){
				const float t = (sliceTop - a[1]) / (b[1] - a[1]);
				const float p[3] = {a[0] + (b[0] - a[0]) * t, sliceTop, a[2] + (b[2] - a[2]) * t};
				add(p);
			}
		}
	}
	if(result.valid){
		// ModelDataはZが鏡像(読み込みの座標系)。エンジンの右手系へはZを反転する(VulkanModelと同じ)
		result.minX = minX;
		result.maxX = maxX;
		result.minZ = -maxZ;
		result.maxZ = -minZ;
	}
	return result;
}

void PropCollision::build(const FieldMap &map, const FootprintFn &footprint)
{
	boxes_.clear();
	gridX_ = std::max(1, static_cast<int>(std::ceil(map.width() * map.cellSize() / kGridSize)));
	gridZ_ = std::max(1, static_cast<int>(std::ceil(map.depth() * map.cellSize() / kGridSize)));
	grid_.assign(static_cast<size_t>(gridX_) * gridZ_, {});
	std::map<std::string, PropFootprint> cache;
	for(const auto &prop : map.props()){
		const std::string &name = map.propName(prop.prop);
		auto found = cache.find(name);
		if(found == cache.end()){
			found = cache.emplace(name, footprint(name)).first;
		}
		const PropFootprint &shape = found->second;
		if(!shape.valid){
			continue;
		}
		Box box;
		box.x = prop.x;
		box.z = prop.z;
		box.cosYaw = std::cos(prop.yaw);
		box.sinYaw = std::sin(prop.yaw);
		box.minX = shape.minX * prop.scale;
		box.maxX = shape.maxX * prop.scale;
		box.minZ = shape.minZ * prop.scale;
		box.maxZ = shape.maxZ * prop.scale;
		const int index = static_cast<int>(boxes_.size());
		boxes_.push_back(box);
		// 回転した長方形を囲む範囲の格子へ入れる
		const float extent = std::max({std::fabs(box.minX), std::fabs(box.maxX)}) + std::max({std::fabs(box.minZ), std::fabs(box.maxZ)});
		const int gx0 = std::clamp(static_cast<int>(std::floor((box.x - extent) / kGridSize)), 0, gridX_ - 1);
		const int gx1 = std::clamp(static_cast<int>(std::floor((box.x + extent) / kGridSize)), 0, gridX_ - 1);
		const int gz0 = std::clamp(static_cast<int>(std::floor((box.z - extent) / kGridSize)), 0, gridZ_ - 1);
		const int gz1 = std::clamp(static_cast<int>(std::floor((box.z + extent) / kGridSize)), 0, gridZ_ - 1);
		for(int gz = gz0; gz <= gz1; ++gz){
			for(int gx = gx0; gx <= gx1; ++gx){
				grid_[static_cast<size_t>(gz) * gridX_ + gx].push_back(index);
			}
		}
	}
}

template<typename Fn>
void PropCollision::forNear(float x, float z, float radius, Fn &&fn) const
{
	if(grid_.empty()){
		return;
	}
	const int gx0 = std::clamp(static_cast<int>(std::floor((x - radius) / kGridSize)), 0, gridX_ - 1);
	const int gx1 = std::clamp(static_cast<int>(std::floor((x + radius) / kGridSize)), 0, gridX_ - 1);
	const int gz0 = std::clamp(static_cast<int>(std::floor((z - radius) / kGridSize)), 0, gridZ_ - 1);
	const int gz1 = std::clamp(static_cast<int>(std::floor((z + radius) / kGridSize)), 0, gridZ_ - 1);
	for(int gz = gz0; gz <= gz1; ++gz){
		for(int gx = gx0; gx <= gx1; ++gx){
			for(const int index : grid_[static_cast<size_t>(gz) * gridX_ + gx]){
				fn(boxes_[static_cast<size_t>(index)]);
			}
		}
	}
}

// 円を、長方形の外へ押し出す。長方形の座標(置いた向きを戻したもの)で、一番近い点から離す。中心が長方形の中なら、一番近い辺の外へ
bool PropCollision::pushOut(const Box &box, float &x, float &z, float radius) const
{
	const float dx = x - box.x, dz = z - box.z;
	float ux = dx * box.cosYaw - dz * box.sinYaw;
	float uz = dx * box.sinYaw + dz * box.cosYaw;
	const float cx = std::clamp(ux, box.minX, box.maxX), cz = std::clamp(uz, box.minZ, box.maxZ);
	float nx = ux - cx, nz = uz - cz;
	const float distance = std::sqrt(nx * nx + nz * nz);
	if(distance >= radius){
		return false;
	}
	if(distance > 1e-5f){
		ux = cx + nx / distance * radius;
		uz = cz + nz / distance * radius;
	}
	else{
		const float left = ux - box.minX, right = box.maxX - ux, front = uz - box.minZ, back = box.maxZ - uz;
		const float best = std::min({left, right, front, back});
		if(best == left){
			ux = box.minX - radius;
		}
		else if(best == right){
			ux = box.maxX + radius;
		}
		else if(best == front){
			uz = box.minZ - radius;
		}
		else{
			uz = box.maxZ + radius;
		}
	}
	x = box.x + ux * box.cosYaw + uz * box.sinYaw;
	z = box.z - ux * box.sinYaw + uz * box.cosYaw;
	return true;
}

bool PropCollision::resolve(float &x, float &z, float radius) const
{
	bool moved = false;
	for(int pass = 0; pass < 4; ++pass){
		bool changed = false;
		forNear(x, z, radius, [&](const Box &box){
			if(pushOut(box, x, z, radius)){
				changed = true;
			}
		});
		moved = moved || changed;
		if(!changed){
			break;
		}
	}
	return moved;
}

bool PropCollision::overlaps(float x, float z, float radius) const
{
	bool hit = false;
	forNear(x, z, radius, [&](const Box &box){
		float px = x, pz = z;
		if(pushOut(box, px, pz, radius)){
			hit = true;
		}
	});
	return hit;
}

} // namespace field
