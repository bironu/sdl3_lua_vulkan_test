#include "field/FieldMap.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <sol/sol.hpp>

namespace field
{

FieldSettings loadFieldSettings(const std::string &relativePath)
{
	FieldSettings result;
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto loaded = lua.safe_script_file(ResourcePaths::resource(relativePath.c_str()), sol::script_pass_on_error);
	if(!loaded.valid()){
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: settings error (%s): %s", relativePath.c_str(), error.what());
		return result;
	}
	if(const sol::optional<sol::table> table = lua["settings"]){
		result.tiles = table->get_or("tiles", result.tiles);
		result.field = table->get_or("field", result.field);
	}
	return result;
}

std::vector<TileDef> loadTileDefs(const std::string &relativePath)
{
	std::vector<TileDef> result;
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto loaded = lua.safe_script_file(ResourcePaths::resource(relativePath.c_str()), sol::script_pass_on_error);
	if(!loaded.valid()){
		const sol::error error = loaded;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: tile definitions error (%s): %s", relativePath.c_str(), error.what());
		return result;
	}
	const sol::optional<sol::table> tiles = lua["tiles"];
	if(!tiles){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: no 'tiles' table in %s", relativePath.c_str());
		return result;
	}
	for(size_t i = 1; i <= tiles->size(); ++i){
		const sol::optional<sol::table> entry = (*tiles)[i];
		if(!entry){
			continue;
		}
		TileDef def;
		def.id = entry->get_or<std::string>("id", "");
		const std::string symbol = entry->get_or<std::string>("symbol", ".");
		def.symbol = symbol.empty() ? '.' : symbol[0];
		def.nameKey = entry->get_or<std::string>("name", "");
		if(const sol::optional<sol::table> color = (*entry)["color"]){
			def.r = static_cast<uint8_t>(color->get_or(1, 128));
			def.g = static_cast<uint8_t>(color->get_or(2, 128));
			def.b = static_cast<uint8_t>(color->get_or(3, 128));
		}
		result.push_back(def);
	}
	return result;
}

FieldMap::FieldMap(int width, int depth, float cellSize)
	: width_(width)
	, depth_(depth)
	, cellSize_(cellSize)
	, cells_(static_cast<size_t>(width) * depth, 0)
{
}

bool FieldMap::set(int x, int z, uint8_t tile)
{
	if(!inside(x, z)){
		return false;
	}
	uint8_t &cell = cells_[static_cast<size_t>(z) * width_ + x];
	if(cell == tile){
		return false;
	}
	cell = tile;
	return true;
}

void FieldMap::fill(uint8_t tile)
{
	std::fill(cells_.begin(), cells_.end(), tile);
}

bool FieldMap::save(const std::string &path, const std::vector<TileDef> &tiles) const
{
	std::ofstream out(path, std::ios::trunc);
	if(!out){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: cannot write %s", path.c_str());
		return false;
	}
	out << "-- フィールドエディタ(field_editor)が書き出した地面のタイル。タイルの記号は res/lua/data/field_tiles.lua の定義\n";
	out << "field = {\n";
	out << "\tcell = " << cellSize_ << ", -- 1マスの大きさ(メートル)\n";
	out << "\twidth = " << width_ << ",\n";
	out << "\tdepth = " << depth_ << ",\n";
	out << "\trows = { -- 1行がzの1列(上から z = 0、1、...)、左から x = 0、1、...\n";
	for(int z = 0; z < depth_; ++z){
		out << "\t\t\"";
		for(int x = 0; x < width_; ++x){
			const size_t tile = get(x, z);
			out << (tile < tiles.size() ? tiles[tile].symbol : '.');
		}
		out << "\",\n";
	}
	out << "\t},\n}\n";
	out.flush();
	return static_cast<bool>(out);
}

bool FieldMap::load(const std::string &path, const std::vector<TileDef> &tiles)
{
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	const auto loaded = lua.safe_script_file(path, sol::script_pass_on_error);
	if(!loaded.valid()){
		return false; // 無いのは普通(新規)なので、ログは出さない
	}
	const sol::optional<sol::table> table = lua["field"];
	if(!table){
		return false;
	}
	const int width = table->get_or("width", 0), depth = table->get_or("depth", 0);
	const sol::optional<sol::table> rows = (*table)["rows"];
	if(width <= 0 || depth <= 0 || !rows){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: invalid field file: %s", path.c_str());
		return false;
	}
	FieldMap loadedMap(width, depth, table->get_or("cell", 1.0f));
	for(int z = 0; z < depth; ++z){
		const std::string row = rows->get_or<std::string>(z + 1, "");
		for(int x = 0; x < width && x < static_cast<int>(row.size()); ++x){
			for(size_t t = 0; t < tiles.size(); ++t){
				if(tiles[t].symbol == row[x]){
					loadedMap.cells_[static_cast<size_t>(z) * width + x] = static_cast<uint8_t>(t);
					break;
				}
			}
		}
	}
	*this = std::move(loadedMap);
	return true;
}

} // namespace field
