#include "scene/character/CharacterList.h"
#include "resources/LuaTable.h"
#include <SDL3/SDL_log.h>
#include <sol/sol.hpp>

namespace game
{

std::vector<CharacterInfo> loadCharacterList(const std::string &relativePath)
{
	std::vector<CharacterInfo> result;
	sol::state lua;
	const sol::optional<sol::table> list = loadLuaTable(lua, relativePath, "characters", "characters");
	if(!list){
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
