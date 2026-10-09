#include "model/GltfFile.h"
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace model
{

namespace
{

size_t componentSize(int type)
{
	switch(type){
	case 5120: case 5121: return 1;
	case 5122: case 5123: return 2;
	case 5125: case 5126: return 4;
	default: return 0;
	}
}

int componentCount(const std::string &type)
{
	if(type == "SCALAR"){ return 1; }
	if(type == "VEC2"){ return 2; }
	if(type == "VEC3"){ return 3; }
	if(type == "VEC4"){ return 4; }
	if(type == "MAT2"){ return 4; }
	if(type == "MAT3"){ return 9; }
	if(type == "MAT4"){ return 16; }
	return 0;
}

float readComponent(const uint8_t *p, int type, bool normalized)
{
	switch(type){
	case 5120:{ const int8_t v = *reinterpret_cast<const int8_t *>(p); return normalized ? std::max(v / 127.0f, -1.0f) : static_cast<float>(v); }
	case 5121:{ const uint8_t v = *p; return normalized ? v / 255.0f : static_cast<float>(v); }
	case 5122:{ int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.0f, -1.0f) : static_cast<float>(v); }
	case 5123:{ uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.0f : static_cast<float>(v); }
	case 5125:{ uint32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v); }
	default:{ float v; std::memcpy(&v, p, 4); return v; }
	}
}

// base64 の文字列を戻す(空白・改行は読み飛ばす。'=' で終わり)。おかしな文字があれば例外
std::vector<uint8_t> decodeBase64(const char *text, size_t length)
{
	std::vector<uint8_t> out;
	out.reserve(length / 4 * 3);
	uint32_t bits = 0;
	int count = 0;
	for(size_t i = 0; i < length; ++i){
		const char c = text[i];
		int v;
		if(c >= 'A' && c <= 'Z'){ v = c - 'A'; }
		else if(c >= 'a' && c <= 'z'){ v = c - 'a' + 26; }
		else if(c >= '0' && c <= '9'){ v = c - '0' + 52; }
		else if(c == '+' || c == '-'){ v = 62; }
		else if(c == '/' || c == '_'){ v = 63; }
		else if(c == '='){ break; }
		else if(c == ' ' || c == '\n' || c == '\r' || c == '\t'){ continue; }
		else{ throw std::runtime_error("bad base64 data"); }
		bits = (bits << 6) | static_cast<uint32_t>(v);
		count += 6;
		if(count >= 8){
			count -= 8;
			out.push_back(static_cast<uint8_t>((bits >> count) & 0xFF));
		}
	}
	return out;
}

} // namespace

void GltfFile::SdlFree::operator()(void *p) const
{
	SDL_free(p);
}

GltfFile::SdlBytes GltfFile::loadFile(const std::string &fullPath, size_t &size)
{
	SdlBytes data(static_cast<uint8_t *>(SDL_LoadFile(fullPath.c_str(), &size)));
	if(!data){
		throw std::runtime_error(std::string("open error. ") + SDL_GetError());
	}
	return data;
}

GltfFile::GltfFile(const std::string &fullPath)
{
	size_t fileSize = 0;
	file_ = loadFile(fullPath, fileSize);
	const uint8_t *data = file_.get();
	const char *jsonText = nullptr;
	size_t jsonLength = 0;
	const bool binary = fileSize >= 4 && std::memcmp(data, "glTF", 4) == 0;
	if(binary){
		if(fileSize < 20){
			throw std::runtime_error("not a glb file");
		}
		uint32_t version;
		std::memcpy(&version, data + 4, 4);
		if(version != 2){
			throw std::runtime_error("unsupported glTF version");
		}
		// チャンク: JSON と BIN
		for(size_t offset = 12; offset + 8 <= fileSize;){
			uint32_t length, type;
			std::memcpy(&length, data + offset, 4);
			std::memcpy(&type, data + offset + 4, 4);
			if(offset + 8 + length > fileSize){
				throw std::runtime_error("chunk out of file range");
			}
			if(type == 0x4E4F534A){ // "JSON"
				jsonText = reinterpret_cast<const char *>(data + offset + 8);
				jsonLength = length;
			}
			else if(type == 0x004E4942 && !bin_){ // "BIN\0"
				bin_ = data + offset + 8;
				binSize_ = length;
			}
			offset += 8 + static_cast<size_t>(length);
		}
		if(!jsonText){
			throw std::runtime_error("no JSON chunk");
		}
	}
	else{
		jsonText = reinterpret_cast<const char *>(data);
		jsonLength = fileSize;
	}
	std::string error;
	if(!parseJson(jsonText, jsonLength, json_, error)){
		throw std::runtime_error(binary ? "JSON: " + error : "not a glb file, and not glTF JSON (" + error + ")");
	}
	// .gltf のバッファ0: data: URI(base64)か、.gltf からの相対パスの外部ファイル
	const std::string uri = json_["buffers"][size_t(0)]["uri"].asString();
	if(!binary && !uri.empty()){
		const std::string marker = ";base64,";
		if(uri.compare(0, 5, "data:") == 0){
			const size_t start = uri.find(marker);
			if(start == std::string::npos){
				throw std::runtime_error("unsupported data URI");
			}
			decoded_ = decodeBase64(uri.data() + start + marker.size(), uri.size() - start - marker.size());
			bin_ = decoded_.data();
			binSize_ = decoded_.size();
		}
		else{
			const size_t slash = fullPath.find_last_of("/\\");
			const std::string path = (slash == std::string::npos ? std::string() : fullPath.substr(0, slash + 1)) + uri;
			try{
				external_ = loadFile(path, binSize_);
			}
			catch(const std::exception &e){
				throw std::runtime_error("buffer " + uri + ": " + e.what());
			}
			bin_ = external_.get();
		}
	}
}

GltfFile::Accessor GltfFile::readAccessor(int index) const
{
	const JsonValue &a = json_["accessors"][static_cast<size_t>(index)];
	if(a.isNull()){
		throw std::runtime_error("bad accessor index");
	}
	Accessor out;
	out.components = componentCount(a["type"].asString());
	out.count = static_cast<size_t>(a["count"].asNumber());
	const int type = a["componentType"].asInt();
	const bool normalized = a["normalized"].asBool();
	if(out.components == 0 || componentSize(type) == 0){
		throw std::runtime_error("bad accessor type");
	}
	out.values.assign(out.count * static_cast<size_t>(out.components), 0.0f);
	if(a["bufferView"].isNumber()){
		readView(a["bufferView"].asInt(), static_cast<size_t>(a["byteOffset"].asNumber()), type, normalized, out.components, out.count, out.values.data());
	}
	const JsonValue &sparse = a["sparse"];
	if(sparse.isObject()){
		const size_t n = static_cast<size_t>(sparse["count"].asNumber());
		std::vector<float> indices(n);
		const JsonValue &idx = sparse["indices"];
		readView(idx["bufferView"].asInt(), static_cast<size_t>(idx["byteOffset"].asNumber()), idx["componentType"].asInt(), false, 1, n, indices.data());
		std::vector<float> values(n * static_cast<size_t>(out.components));
		const JsonValue &val = sparse["values"];
		readView(val["bufferView"].asInt(), static_cast<size_t>(val["byteOffset"].asNumber()), type, normalized, out.components, n, values.data());
		for(size_t i = 0; i < n; ++i){
			const size_t target = static_cast<size_t>(indices[i]);
			if(target >= out.count){
				throw std::runtime_error("sparse index out of range");
			}
			for(int c = 0; c < out.components; ++c){
				out.values[target * static_cast<size_t>(out.components) + static_cast<size_t>(c)] = values[i * static_cast<size_t>(out.components) + static_cast<size_t>(c)];
			}
		}
	}
	return out;
}

// bufferViewの先頭からbyteOffsetの位置にある、count個の要素(成分数components)をfloatに読む
void GltfFile::readView(int view, size_t byteOffset, int type, bool normalized, int components, size_t count, float *out) const
{
	const JsonValue &v = json_["bufferViews"][static_cast<size_t>(view)];
	if(v.isNull() || !bin_){
		throw std::runtime_error("bad bufferView index");
	}
	const size_t start = static_cast<size_t>(v["byteOffset"].asNumber()) + byteOffset;
	const size_t size = componentSize(type);
	size_t stride = static_cast<size_t>(v["byteStride"].asNumber());
	const size_t elementSize = size * static_cast<size_t>(components);
	if(stride == 0){
		stride = elementSize;
	}
	if(count > 0 && start + stride * (count - 1) + elementSize > binSize_){
		throw std::runtime_error("accessor out of buffer range");
	}
	for(size_t i = 0; i < count; ++i){
		for(int c = 0; c < components; ++c){
			out[i * static_cast<size_t>(components) + static_cast<size_t>(c)] = readComponent(bin_ + start + stride * i + size * static_cast<size_t>(c), type, normalized);
		}
	}
}

bool GltfFile::viewBytes(int view, std::vector<uint8_t> &out) const
{
	const JsonValue &v = json_["bufferViews"][static_cast<size_t>(view)];
	if(v.isNull() || !bin_){
		return false;
	}
	const size_t start = static_cast<size_t>(v["byteOffset"].asNumber());
	const size_t length = static_cast<size_t>(v["byteLength"].asNumber());
	if(start + length > binSize_){
		return false;
	}
	out.assign(bin_ + start, bin_ + start + length);
	return true;
}

std::vector<int> GltfFile::nodeParents() const
{
	const JsonValue &nodes = json_["nodes"];
	const size_t nodeCount = nodes.size();
	std::vector<int> parent(nodeCount, -1);
	for(size_t i = 0; i < nodeCount; ++i){
		const JsonValue &children = nodes[i]["children"];
		for(size_t k = 0; k < children.size(); ++k){
			const int c = children[k].asInt(-1);
			if(c >= 0 && static_cast<size_t>(c) < nodeCount){
				parent[static_cast<size_t>(c)] = static_cast<int>(i);
			}
		}
	}
	return parent;
}

std::vector<int> GltfFile::nodeOrder(const std::vector<int> &parents) const
{
	const JsonValue &nodes = json_["nodes"];
	const size_t nodeCount = nodes.size();
	std::vector<int> order;
	std::vector<int> stack;
	const JsonValue &sceneNodes = json_["scenes"][static_cast<size_t>(json_["scene"].asInt(0))]["nodes"];
	for(size_t k = sceneNodes.size(); k > 0; --k){
		stack.push_back(sceneNodes[k - 1].asInt());
	}
	if(stack.empty()){ // シーンの指定が無ければ、親の無いノードすべて
		for(size_t i = nodeCount; i > 0; --i){
			if(parents[i - 1] < 0){ stack.push_back(static_cast<int>(i - 1)); }
		}
	}
	std::vector<bool> visited(nodeCount, false);
	while(!stack.empty()){
		const int n = stack.back();
		stack.pop_back();
		if(n < 0 || static_cast<size_t>(n) >= nodeCount || visited[static_cast<size_t>(n)]){
			continue;
		}
		visited[static_cast<size_t>(n)] = true;
		order.push_back(n);
		const JsonValue &children = nodes[static_cast<size_t>(n)]["children"];
		for(size_t k = children.size(); k > 0; --k){
			stack.push_back(children[k - 1].asInt(-1));
		}
	}
	return order;
}

GltfFile::NodeTransform GltfFile::nodeTransform(size_t node) const
{
	const JsonValue &n = json_["nodes"][node];
	NodeTransform out;
	for(size_t k = 0; k < 3; ++k){
		out.translation[k] = n["translation"][k].asNumber(out.translation[k]);
		out.scale[k] = n["scale"][k].asNumber(out.scale[k]);
	}
	for(size_t k = 0; k < 4; ++k){
		out.rotation[k] = n["rotation"][k].asNumber(out.rotation[k]);
	}
	if(n["matrix"].isArray() && n["matrix"].size() == 16){
		out.hasMatrix = true;
		for(size_t k = 0; k < 16; ++k){ out.matrix[k] = n["matrix"][k].asNumber(); }
		out.rotation[0] = out.rotation[1] = out.rotation[2] = 0.0; // 行列で指定されたノードの回転は分解しない(回転なしとして扱う)
		out.rotation[3] = 1.0;
	}
	return out;
}

GltfAxes GltfAxes::of(const JsonValue &json)
{
	return json["extensions"]["VRM"].isObject() ? GltfAxes{{-1.0, 1.0, 1.0}} : GltfAxes{{1.0, 1.0, -1.0}};
}

} // namespace model
