#include "scene/game/GameSettings.h"
#include "resources/ResourcePaths.h"
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
}

GameSettings loadGameSettings(const std::string &relativePath)
{
	GameSettings result;
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto loaded = lua.safe_script_file(ResourcePaths::resource(relativePath.c_str()), sol::script_pass_on_error);
	if(!loaded.valid()){
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "game settings error (%s): %s", relativePath.c_str(), error.what());
		return result;
	}
	const sol::optional<sol::table> settings = lua["settings"];
	if(!settings){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "game settings: no 'settings' table in %s", relativePath.c_str());
		return result;
	}
	const sol::optional<sol::table> player = (*settings)["player"], input = (*settings)["input"], camera = (*settings)["camera"], fade = (*settings)["fade"];
	readFloat(player, "radius", result.player.radius);
	readFloat(player, "walkSpeed", result.player.walkSpeed);
	readFloat(player, "slowRunSpeed", result.player.slowRunSpeed);
	readFloat(player, "fastRunSpeed", result.player.fastRunSpeed);
	readFloat(player, "climbSpeed", result.player.climbSpeed);
	readFloat(player, "climbEnter", result.player.climbEnter);
	readFloat(player, "rollDistance", result.player.rollDistance);
	readFloat(player, "turnSpeed", result.player.turnSpeed);
	readFloat(input, "runStick", result.input.runStick);
	readFloat(input, "tapTime", result.input.tapTime);
	readFloat(input, "triggerOn", result.input.triggerOn);
	readFloat(camera, "referenceHeight", result.camera.referenceHeight);
	readFloat(camera, "height", result.camera.height);
	readFloat(camera, "minEyeHeight", result.camera.minEyeHeight);
	readFloat(camera, "headTop", result.camera.headTop);
	readFloat(camera, "distance", result.camera.distance);
	readFloat(camera, "minDistance", result.camera.minDistance);
	readFloat(camera, "maxDistance", result.camera.maxDistance);
	readFloat(camera, "yawSpeed", result.camera.yawSpeed);
	readFloat(camera, "pitchSpeed", result.camera.pitchSpeed);
	readFloat(camera, "minArm", result.camera.minArm);
	readFloat(camera, "armRecover", result.camera.armRecover);
	readFloat(camera, "bodyDistance", result.camera.bodyDistance);
	readFloat(camera, "approachFraction", result.camera.approachFraction);
	readFloat(fade, "toAction", result.fade.toAction);
	readFloat(fade, "fromAction", result.fade.fromAction);
	readFloat(fade, "locomotion", result.fade.locomotion);
	return result;
}

} // namespace game
