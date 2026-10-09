#include "scene/game/GameSettings.h"
#include "resources/LuaTable.h"
#include <SDL3/SDL_log.h>
#include <sol/sol.hpp>

namespace game
{

namespace
{
// table[name] が表なら、その中の key を value へ読む(無ければ、そのまま)
void readFloat(const sol::optional<sol::table> &table, const char *key, float &value)
{
	if(table){
		value = table->get_or(key, value);
	}
}

void readBool(const sol::optional<sol::table> &table, const char *key, bool &value)
{
	if(table){
		value = table->get_or(key, value);
	}
}
}

GameSettings loadGameSettings(const std::string &relativePath)
{
	GameSettings result;
	sol::state lua;
	const sol::optional<sol::table> settings = loadLuaTable(lua, relativePath, "settings", "game settings");
	if(!settings){
		return result;
	}
	const sol::optional<sol::table> motion = (*settings)["motion"], climb = (*settings)["climb"], player = (*settings)["player"], input = (*settings)["input"], camera = (*settings)["camera"], fade = (*settings)["fade"];
	readFloat(player, "radius", result.player.radius);
	readFloat(player, "walkSpeed", result.player.walkSpeed);
	readFloat(player, "slowRunSpeed", result.player.slowRunSpeed);
	readFloat(player, "fastRunSpeed", result.player.fastRunSpeed);
	readFloat(player, "climbSpeed", result.player.climbSpeed);
	readFloat(player, "rollDistance", result.player.rollDistance);
	readFloat(player, "rollRate", result.player.rollRate);
	readFloat(player, "rollStartOffset", result.player.rollStartOffset);
	readFloat(player, "rollCancelProgress", result.player.rollCancelProgress);
	readFloat(player, "rollMotionTravel", result.player.rollMotionTravel);
	readFloat(player, "shadowRadius", result.player.shadowRadius);
	readFloat(player, "shadowOpacity", result.player.shadowOpacity);
	readFloat(player, "turnSpeed", result.player.turnSpeed);
	readFloat(motion, "seamThreshold", result.motion.seamThreshold);
	readFloat(motion, "loopBlend", result.motion.loopBlend);
	readFloat(climb, "tiltFactor", result.climb.tiltFactor);
	readFloat(climb, "tiltSmooth", result.climb.tiltSmooth);
	readFloat(climb, "blendLow", result.climb.blendLow);
	readFloat(climb, "blendHigh", result.climb.blendHigh);
	readFloat(climb, "riseRate", result.climb.riseRate);
	readFloat(climb, "fallRate", result.climb.fallRate);
	readFloat(input, "runStick", result.input.runStick);
	readFloat(input, "runExit", result.input.runExit);
	readFloat(input, "runGrace", result.input.runGrace);
	readFloat(input, "moveEnter", result.input.moveEnter);
	readFloat(input, "moveExit", result.input.moveExit);
	readFloat(input, "tapTime", result.input.tapTime);
	readFloat(input, "triggerOn", result.input.triggerOn);
	readBool(input, "rollOnPress", result.input.rollOnPress);
	readFloat(camera, "referenceHeight", result.camera.referenceHeight);
	readFloat(camera, "height", result.camera.height);
	readFloat(camera, "minEyeHeight", result.camera.minEyeHeight);
	readFloat(camera, "headTop", result.camera.headTop);
	readFloat(camera, "distance", result.camera.distance);
	readFloat(camera, "yawSpeed", result.camera.yawSpeed);
	readFloat(camera, "pitchSpeed", result.camera.pitchSpeed);
	readFloat(camera, "minArm", result.camera.minArm);
	readFloat(camera, "armRecover", result.camera.armRecover);
	readFloat(camera, "bodyDistance", result.camera.bodyDistance);
	readFloat(camera, "fov", result.camera.fov);
	readFloat(camera, "near", result.camera.nearPlane);
	readFloat(camera, "far", result.camera.farPlane);
	readFloat(camera, "mouseSpeed", result.camera.mouseSpeed);
	readFloat(camera, "approachFraction", result.camera.approachFraction);
	readFloat(fade, "toAction", result.fade.toAction);
	readFloat(fade, "fromAction", result.fade.fromAction);
	readFloat(fade, "fromActionMoving", result.fade.fromActionMoving);
	readFloat(fade, "locomotion", result.fade.locomotion);
	return result;
}

std::vector<std::string> loadMotionPaths(const std::string &manifestPath, const std::vector<std::string> &names)
{
	std::vector<std::string> paths(names.size());
	sol::state lua;
	const sol::optional<sol::table> motions = loadLuaTable(lua, manifestPath, "motions", "motion table");
	if(!motions){
		return paths;
	}
	for(size_t i = 0; i < names.size(); ++i){
		const sol::optional<std::string> path = (*motions)[names[i]];
		paths[i] = path.value_or(std::string());
		if(paths[i].empty()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "motion table: no motion '%s' in %s", names[i].c_str(), manifestPath.c_str());
		}
	}
	return paths;
}

} // namespace game
