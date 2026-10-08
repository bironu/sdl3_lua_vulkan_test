#include "field/FieldMovement.h"
#include <cmath>

namespace field
{

MovementRules MovementRules::fromTiles(const std::vector<TileDef> &tiles, float maxSlope, float maxClimbSlope)
{
	MovementRules rules;
	rules.maxSlope = maxSlope;
	rules.maxClimbSlope = maxClimbSlope;
	for(const auto &tile : tiles){
		rules.tileWalkable.push_back(tile.walkable);
	}
	return rules;
}

namespace
{
bool tileWalkable(const FieldMap &map, const MovementRules &rules, float x, float z)
{
	const int cx = static_cast<int>(std::floor(x / map.cellSize())), cz = static_cast<int>(std::floor(z / map.cellSize()));
	if(!map.inside(cx, cz)){
		return true; // フィールドの外は、縁の制限(呼び出し側)に任せる
	}
	const size_t tile = map.get(cx, cz);
	return tile >= rules.tileWalkable.size() || rules.tileWalkable[tile];
}

bool step(const FieldMap &map, const MovementRules &rules, float radius, bool tileRuleOn, float limit, float x, float z, float toX, float toZ)
{
	if(tileRuleOn && !canStandAt(map, rules, toX, toZ, radius)){
		return false;
	}
	const float dx = toX - x, dz = toZ - z;
	const float distance = std::sqrt(dx * dx + dz * dz);
	if(distance > 1e-6f){
		const float rise = map.heightAt(toX, toZ) - map.heightAt(x, z);
		if(rise / distance > limit){ // 登りだけ止める(急な所からの下りは、通す: 急な丘の上から降りられなくならないように)
			return false;
		}
	}
	return true;
}
}

bool canStandAt(const FieldMap &map, const MovementRules &rules, float x, float z, float radius)
{
	return tileWalkable(map, rules, x, z) && tileWalkable(map, rules, x - radius, z) && tileWalkable(map, rules, x + radius, z)
		&& tileWalkable(map, rules, x, z - radius) && tileWalkable(map, rules, x, z + radius);
}

bool moveOnField(const FieldMap &map, const MovementRules &rules, float radius, float &x, float &z, float toX, float toZ, float maxSlope)
{
	const float limit = maxSlope >= 0.0f ? maxSlope : rules.maxSlope;
	const bool tileRuleOn = canStandAt(map, rules, x, z, radius);
	if(step(map, rules, radius, tileRuleOn, limit, x, z, toX, toZ)){
		x = toX;
		z = toZ;
		return true;
	}
	// 滑る: 軸ごとに試す(動きの大きい軸を先に)
	const bool xFirst = std::fabs(toX - x) >= std::fabs(toZ - z);
	for(int pass = 0; pass < 2; ++pass){
		if((pass == 0) == xFirst){
			if(toX != x && step(map, rules, radius, tileRuleOn, limit, x, z, toX, z)){
				x = toX;
				return true;
			}
		}
		else if(toZ != z && step(map, rules, radius, tileRuleOn, limit, x, z, x, toZ)){
			z = toZ;
			return true;
		}
	}
	return false;
}

} // namespace field

namespace field
{
float slopeAhead(const FieldMap &map, float x, float z, float dirX, float dirZ, float probe)
{
	return (map.heightAt(x + dirX * probe, z + dirZ * probe) - map.heightAt(x, z)) / probe;
}
}
