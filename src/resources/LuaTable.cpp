#include "resources/LuaTable.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>

sol::optional<sol::table> loadLuaTable(sol::state &lua, const std::string &relativePath, const char *tableName, const char *logTag)
{
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto loaded = lua.safe_script_file(ResourcePaths::resource(relativePath.c_str()), sol::script_pass_on_error);
	if(!loaded.valid()){
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s error (%s): %s", logTag, relativePath.c_str(), error.what());
		return sol::nullopt;
	}
	const sol::optional<sol::table> table = lua[tableName];
	if(!table){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: no '%s' table in %s", logTag, tableName, relativePath.c_str());
	}
	return table;
}
