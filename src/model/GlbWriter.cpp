#include "model/GlbWriter.h"
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace model
{

int GlbWriter::addAccessor(const void *data, size_t count, int componentType, int components, const char *type, const std::string &extra)
{
	const size_t componentSize = componentType == 5123 ? 2 : 4;
	while(bin_.size() % 4 != 0){ bin_.push_back(0); }
	const size_t offset = bin_.size();
	const size_t bytes = count * static_cast<size_t>(components) * componentSize;
	bin_.resize(offset + bytes);
	if(bytes > 0){
		std::memcpy(bin_.data() + offset, data, bytes);
	}
	char view[160];
	std::snprintf(view, sizeof(view), "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", offset, bytes);
	views_.push_back(view);
	accessors_.push_back("{\"bufferView\":" + std::to_string(views_.size() - 1) + ",\"componentType\":" + std::to_string(componentType)
		+ ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" + extra + "}");
	return static_cast<int>(accessors_.size()) - 1;
}

int GlbWriter::addAccessor(const std::vector<float> &data, int components, const char *type, bool withMinMax)
{
	std::string extra;
	if(withMinMax && !data.empty()){
		const auto mm = std::minmax_element(data.begin(), data.end());
		extra = ",\"min\":[" + number(*mm.first) + "],\"max\":[" + number(*mm.second) + "]";
	}
	return addAccessor(data.data(), data.size() / static_cast<size_t>(components), 5126, components, type, extra);
}

bool GlbWriter::save(const std::string &fullPath, std::string json) const
{
	while(json.size() % 4 != 0){ json += ' '; }
	std::vector<uint8_t> bin = bin_;
	while(bin.size() % 4 != 0){ bin.push_back(0); }

	std::vector<uint8_t> out;
	out.reserve(28 + json.size() + bin.size());
	auto put32 = [&](uint32_t v){ for(int i = 0; i < 4; ++i){ out.push_back(static_cast<uint8_t>(v >> (8 * i))); } };
	out.insert(out.end(), {'g', 'l', 'T', 'F'});
	put32(2);
	put32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
	put32(static_cast<uint32_t>(json.size()));
	out.insert(out.end(), {'J', 'S', 'O', 'N'});
	out.insert(out.end(), json.begin(), json.end());
	put32(static_cast<uint32_t>(bin.size()));
	out.insert(out.end(), {'B', 'I', 'N', 0});
	out.insert(out.end(), bin.begin(), bin.end());

	SDL_IOStream *io = SDL_IOFromFile(fullPath.c_str(), "wb");
	if(!io){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "glb open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return false;
	}
	const bool ok = SDL_WriteIO(io, out.data(), out.size()) == out.size();
	SDL_CloseIO(io);
	if(!ok){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "glb write error. %s (%s)", SDL_GetError(), fullPath.c_str());
	}
	return ok;
}

std::string GlbWriter::number(float v)
{
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%.9g", v);
	return buf;
}

std::string GlbWriter::join(const std::vector<std::string> &items)
{
	std::string out;
	for(size_t i = 0; i < items.size(); ++i){
		out += (i ? "," : "") + items[i];
	}
	return out;
}

} // namespace model
