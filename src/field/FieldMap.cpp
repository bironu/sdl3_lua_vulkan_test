#include "field/FieldMap.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstring>
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
		result.maxSlope = table->get_or("maxSlope", result.maxSlope);
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
		def.walkable = entry->get_or("walkable", true);
		if(const sol::optional<sol::table> color = (*entry)["color"]){
			def.r = static_cast<uint8_t>(color->get_or(1, 128));
			def.g = static_cast<uint8_t>(color->get_or(2, 128));
			def.b = static_cast<uint8_t>(color->get_or(3, 128));
		}
		result.push_back(def);
	}
	return result;
}

namespace
{
constexpr char kMagic[4] = {'F', 'L', 'D', '1'};

template<typename T>
void writeRaw(std::ostream &out, T value)
{
	out.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

template<typename T>
bool readRaw(std::istream &in, T &value)
{
	in.read(reinterpret_cast<char *>(&value), sizeof(T));
	return static_cast<bool>(in);
}

void putString(std::ostream &out, const std::string &text)
{
	writeRaw<uint16_t>(out, static_cast<uint16_t>(text.size()));
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool getString(std::istream &in, std::string &text)
{
	uint16_t length = 0;
	if(!readRaw(in, length)){
		return false;
	}
	text.resize(length);
	in.read(text.data(), length);
	return static_cast<bool>(in);
}
}

FieldMap::FieldMap(int width, int depth, float cellSize)
	: width_(width)
	, depth_(depth)
	, cellSize_(cellSize)
	, cells_(static_cast<size_t>(width) * depth, 0)
	, heights_(static_cast<size_t>(width + 1) * (depth + 1), 0)
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

int16_t FieldMap::rawHeight(int vx, int vz) const
{
	vx = std::clamp(vx, 0, width_);
	vz = std::clamp(vz, 0, depth_);
	return heights_[static_cast<size_t>(vertexIndex(vx, vz))];
}

bool FieldMap::setVertexHeight(int vx, int vz, float meters)
{
	if(!insideVertex(vx, vz)){
		return false;
	}
	const float scaled = std::clamp(std::round(meters * kHeightScale), -32767.0f, 32767.0f);
	int16_t &value = heights_[static_cast<size_t>(vertexIndex(vx, vz))];
	if(value == static_cast<int16_t>(scaled)){
		return false;
	}
	value = static_cast<int16_t>(scaled);
	return true;
}

void FieldMap::fillHeight(float meters)
{
	const float scaled = std::clamp(std::round(meters * kHeightScale), -32767.0f, 32767.0f);
	std::fill(heights_.begin(), heights_.end(), static_cast<int16_t>(scaled));
}

float FieldMap::heightAt(float x, float z) const
{
	const float gx = std::clamp(x / cellSize_, 0.0f, static_cast<float>(width_));
	const float gz = std::clamp(z / cellSize_, 0.0f, static_cast<float>(depth_));
	const int cx = std::min(static_cast<int>(gx), width_ - 1), cz = std::min(static_cast<int>(gz), depth_ - 1);
	const float fx = gx - cx, fz = gz - cz;
	const float h00 = vertexHeight(cx, cz), h10 = vertexHeight(cx + 1, cz), h01 = vertexHeight(cx, cz + 1), h11 = vertexHeight(cx + 1, cz + 1);
	// (0,0)-(1,1)の対角線で分けた2枚の三角形(描画のインデックスと同じ)
	if(fx >= fz){
		return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
	}
	return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

void FieldMap::vertexNormal(int vx, int vz, float out[3]) const
{
	const float dx = (vertexHeight(vx + 1, vz) - vertexHeight(vx - 1, vz)) / (2.0f * cellSize_);
	const float dz = (vertexHeight(vx, vz + 1) - vertexHeight(vx, vz - 1)) / (2.0f * cellSize_);
	const float length = std::sqrt(dx * dx + 1.0f + dz * dz);
	out[0] = -dx / length;
	out[1] = 1.0f / length;
	out[2] = -dz / length;
}

void FieldMap::normalAt(float x, float z, float out[3]) const
{
	const float gx = std::clamp(x / cellSize_, 0.0f, static_cast<float>(width_));
	const float gz = std::clamp(z / cellSize_, 0.0f, static_cast<float>(depth_));
	const int cx = std::min(static_cast<int>(gx), width_ - 1), cz = std::min(static_cast<int>(gz), depth_ - 1);
	const float fx = gx - cx, fz = gz - cz;
	float n00[3], n10[3], n01[3], n11[3];
	vertexNormal(cx, cz, n00);
	vertexNormal(cx + 1, cz, n10);
	vertexNormal(cx, cz + 1, n01);
	vertexNormal(cx + 1, cz + 1, n11);
	float length = 0.0f;
	for(int k = 0; k < 3; ++k){
		out[k] = (n00[k] * (1 - fx) + n10[k] * fx) * (1 - fz) + (n01[k] * (1 - fx) + n11[k] * fx) * fz;
		length += out[k] * out[k];
	}
	length = std::sqrt(length);
	for(int k = 0; k < 3; ++k){
		out[k] /= length;
	}
}

bool FieldMap::raycast(const float origin[3], const float dir[3], float maxDistance, float &t, float hit[3]) const
{
	const float fieldW = width_ * cellSize_, fieldD = depth_ * cellSize_;
	const float step = std::max(cellSize_ * 0.4f, 0.1f);
	auto above = [&](float s, bool &inField){
		const float x = origin[0] + dir[0] * s, y = origin[1] + dir[1] * s, z = origin[2] + dir[2] * s;
		inField = x >= 0.0f && z >= 0.0f && x <= fieldW && z <= fieldD;
		return y - (inField ? heightAt(x, z) : -1e9f);
	};
	bool inField = false;
	float previousT = 0.0f, previous = above(0.0f, inField);
	bool previousIn = inField;
	for(float s = step; s <= maxDistance + step; s += step){
		const float current = above(std::min(s, maxDistance), inField);
		if(inField && previousIn && previous > 0.0f && current <= 0.0f){
			float lo = previousT, hi = std::min(s, maxDistance);
			for(int i = 0; i < 12; ++i){
				const float mid = (lo + hi) * 0.5f;
				bool dummy;
				if(above(mid, dummy) > 0.0f){
					lo = mid;
				}
				else{
					hi = mid;
				}
			}
			t = (lo + hi) * 0.5f;
			for(int k = 0; k < 3; ++k){
				hit[k] = origin[k] + dir[k] * t;
			}
			return true;
		}
		previousT = std::min(s, maxDistance);
		previous = current;
		previousIn = inField;
	}
	return false;
}

uint16_t FieldMap::propId(const std::string &name)
{
	for(size_t i = 0; i < propNames_.size(); ++i){
		if(propNames_[i] == name){
			return static_cast<uint16_t>(i);
		}
	}
	propNames_.push_back(name);
	return static_cast<uint16_t>(propNames_.size() - 1);
}

size_t FieldMap::addProp(const std::string &name, float x, float z, float yaw, float scale, float lift)
{
	PlacedProp prop;
	prop.prop = propId(name);
	prop.x = x;
	prop.y = lift;
	prop.z = z;
	prop.yaw = yaw;
	prop.scale = scale;
	props_.push_back(prop);
	return props_.size() - 1;
}

void FieldMap::insertProp(size_t index, const PlacedProp &prop)
{
	props_.insert(props_.begin() + static_cast<std::ptrdiff_t>(std::min(index, props_.size())), prop);
}

PlacedProp FieldMap::removeProp(size_t index)
{
	const PlacedProp removed = props_[index];
	props_.erase(props_.begin() + static_cast<std::ptrdiff_t>(index));
	return removed;
}

// ファイルの構成(すべてリトルエンディアンのまま書く): "FLD1", width(u32), depth(u32), cellSize(f32),
// タイル名の表(u16 個数、各: 文字列)、マスのタイル(u8 x width*depth。表の番号)、頂点の高さ(i16 x (width+1)*(depth+1))、
// 物の名前の表(u16 個数、各: 文字列)、物(u32 個数、各: 名前の番号u16、x,y,z,yaw,scale の f32)。文字列は u16 の長さ+バイト列
bool FieldMap::save(const std::string &path, const std::vector<TileDef> &tiles) const
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if(!out){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: cannot write %s", path.c_str());
		return false;
	}
	out.write(kMagic, sizeof(kMagic));
	writeRaw<uint32_t>(out, static_cast<uint32_t>(width_));
	writeRaw<uint32_t>(out, static_cast<uint32_t>(depth_));
	writeRaw<float>(out, cellSize_);
	writeRaw<uint16_t>(out, static_cast<uint16_t>(tiles.size()));
	for(const auto &tile : tiles){
		putString(out, tile.id);
	}
	out.write(reinterpret_cast<const char *>(cells_.data()), static_cast<std::streamsize>(cells_.size()));
	out.write(reinterpret_cast<const char *>(heights_.data()), static_cast<std::streamsize>(heights_.size() * sizeof(int16_t)));
	// 使っている物の名前だけ、詰めて持つ
	std::vector<int> remap(propNames_.size(), -1);
	std::vector<std::string> usedNames;
	for(const auto &prop : props_){
		if(remap[prop.prop] < 0){
			remap[prop.prop] = static_cast<int>(usedNames.size());
			usedNames.push_back(propNames_[prop.prop]);
		}
	}
	writeRaw<uint16_t>(out, static_cast<uint16_t>(usedNames.size()));
	for(const auto &name : usedNames){
		putString(out, name);
	}
	writeRaw<uint32_t>(out, static_cast<uint32_t>(props_.size()));
	for(const auto &prop : props_){
		writeRaw<uint16_t>(out, static_cast<uint16_t>(remap[prop.prop]));
		writeRaw<float>(out, prop.x);
		writeRaw<float>(out, prop.y);
		writeRaw<float>(out, prop.z);
		writeRaw<float>(out, prop.yaw);
		writeRaw<float>(out, prop.scale);
	}
	out.flush();
	return static_cast<bool>(out);
}

bool FieldMap::load(const std::string &path, const std::vector<TileDef> &tiles)
{
	std::ifstream in(path, std::ios::binary);
	if(!in){
		return false; // 無いのは普通(新規)なので、ログは出さない
	}
	auto invalid = [&]{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "field: invalid field file: %s", path.c_str());
		return false;
	};
	char magic[4];
	in.read(magic, sizeof(magic));
	if(!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0){
		return invalid();
	}
	uint32_t width = 0, depth = 0;
	float cell = 1.0f;
	if(!readRaw(in, width) || !readRaw(in, depth) || !readRaw(in, cell) || width == 0 || depth == 0 || width > 4096 || depth > 4096 || !(cell > 0.0f)){
		return invalid();
	}
	FieldMap loaded(static_cast<int>(width), static_cast<int>(depth), cell);
	uint16_t tileCount = 0;
	if(!readRaw(in, tileCount)){
		return invalid();
	}
	std::vector<uint8_t> tileRemap(tileCount, 0); // ファイルの表の番号 → いまのタイルの番号
	for(uint16_t i = 0; i < tileCount; ++i){
		std::string id;
		if(!getString(in, id)){
			return invalid();
		}
		for(size_t t = 0; t < tiles.size(); ++t){
			if(tiles[t].id == id){
				tileRemap[i] = static_cast<uint8_t>(t);
				break;
			}
		}
	}
	in.read(reinterpret_cast<char *>(loaded.cells_.data()), static_cast<std::streamsize>(loaded.cells_.size()));
	in.read(reinterpret_cast<char *>(loaded.heights_.data()), static_cast<std::streamsize>(loaded.heights_.size() * sizeof(int16_t)));
	if(!in){
		return invalid();
	}
	for(auto &cellValue : loaded.cells_){
		cellValue = cellValue < tileRemap.size() ? tileRemap[cellValue] : 0;
	}
	uint16_t nameCount = 0;
	if(!readRaw(in, nameCount)){
		return invalid();
	}
	for(uint16_t i = 0; i < nameCount; ++i){
		std::string name;
		if(!getString(in, name)){
			return invalid();
		}
		loaded.propNames_.push_back(std::move(name));
	}
	uint32_t propCount = 0;
	if(!readRaw(in, propCount)){
		return invalid();
	}
	loaded.props_.reserve(propCount);
	for(uint32_t i = 0; i < propCount; ++i){
		PlacedProp prop;
		if(!readRaw(in, prop.prop) || !readRaw(in, prop.x) || !readRaw(in, prop.y) || !readRaw(in, prop.z) || !readRaw(in, prop.yaw) || !readRaw(in, prop.scale)
			|| prop.prop >= loaded.propNames_.size()){
			return invalid();
		}
		loaded.props_.push_back(prop);
	}
	*this = std::move(loaded);
	return true;
}

} // namespace field
