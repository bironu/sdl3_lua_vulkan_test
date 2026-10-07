#include "scene/character/CharacterList.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>
#include <sol/sol.hpp>

namespace game
{

std::vector<CharacterInfo> loadCharacterList(const std::string &relativePath)
{
	std::vector<CharacterInfo> result;
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto loaded = lua.safe_script_file(ResourcePaths::resource(relativePath.c_str()), sol::script_pass_on_error);
	if(!loaded.valid()){
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "characters: script error (%s): %s", relativePath.c_str(), error.what());
		return result;
	}
	const sol::optional<sol::table> list = lua["characters"];
	if(!list){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "characters: no 'characters' table in %s", relativePath.c_str());
		return result;
	}
	for(size_t i = 1; i <= list->size(); ++i){
		const sol::optional<sol::table> entry = (*list)[i];
		if(!entry){
			continue;
		}
		CharacterInfo info;
		info.model = entry->get_or<std::string>("model", "");
		if(info.model.empty()){
			continue;
		}
		info.name = entry->get_or<std::string>("name", info.model);
		info.motion = entry->get_or<std::string>("motion", "");
		result.push_back(std::move(info));
	}
	return result;
}

} // namespace game
