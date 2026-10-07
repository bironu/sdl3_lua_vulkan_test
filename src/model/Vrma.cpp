#include "model/Vrma.h"
#include "model/Json.h"
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace model
{

namespace
{

// VRMAは、モデルが何であれ、正面が+ZのglTF座標(右手系)。このエンジンのモデルは、PMX・VRMともに、正面が-Zの左手系に直してあるので、
// VRMAとの変換は、常にZ方向の鏡像(VRM 0.xは正面が-Zだが、読み込み時にX方向の鏡像にしてあり、そのZ方向の鏡像は、
// 「Y軸まわりに180度回して+Z向きにする」と同じ結果になる)
constexpr float kAnimationAxisSign[3] = {1.0f, 1.0f, -1.0f};

// 座標系の符号の鏡像: 位置は各軸に符号を掛け、回転(x,y,z,w)は、他の2軸の符号の積を掛ける
Quat mirrorQuat(const Quat &q, const float s[3])
{
	return {s[1] * s[2] * q.x, s[0] * s[2] * q.y, s[0] * s[1] * q.z, q.w};
}

Vec3 mirrorPoint(const Vec3 &p, const float s[3])
{
	return {p.x * s[0], p.y * s[1], p.z * s[2]};
}

// 時刻timesの列から、時刻tを挟む2つの番号とその間の割合を求める
void locate(const std::vector<float> &times, float t, size_t &i0, size_t &i1, float &alpha)
{
	if(times.empty()){
		i0 = i1 = 0;
		alpha = 0.0f;
		return;
	}
	const auto upper = std::upper_bound(times.begin(), times.end(), t);
	if(upper == times.begin()){
		i0 = i1 = 0;
		alpha = 0.0f;
	}
	else if(upper == times.end()){
		i0 = i1 = times.size() - 1;
		alpha = 0.0f;
	}
	else{
		i1 = static_cast<size_t>(upper - times.begin());
		i0 = i1 - 1;
		const float span = times[i1] - times[i0];
		alpha = span > 1e-9f ? (t - times[i0]) / span : 0.0f;
	}
}

// ---- 書き出し ----

class GlbWriter
{
public:
	// 時刻・値の配列をバイナリに足し、accessorの番号を返す。nameで型を決める(SCALAR/VEC3/VEC4)
	int addAccessor(const std::vector<float> &data, int components, const char *type, bool withMinMax)
	{
		while(bin_.size() % 4 != 0){ bin_.push_back(0); }
		const size_t offset = bin_.size();
		const size_t bytes = data.size() * sizeof(float);
		bin_.resize(offset + bytes);
		std::memcpy(bin_.data() + offset, data.data(), bytes);
		char view[160];
		std::snprintf(view, sizeof(view), "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", offset, bytes);
		views_.push_back(view);
		const size_t count = data.size() / static_cast<size_t>(components);
		std::string accessor = "{\"bufferView\":" + std::to_string(views_.size() - 1) + ",\"componentType\":5126,\"count\":" + std::to_string(count)
			+ ",\"type\":\"" + type + "\"";
		if(withMinMax && !data.empty()){
			const auto mm = std::minmax_element(data.begin(), data.end());
			char buf[96];
			std::snprintf(buf, sizeof(buf), ",\"min\":[%.9g],\"max\":[%.9g]", *mm.first, *mm.second);
			accessor += buf;
		}
		accessor += "}";
		accessors_.push_back(accessor);
		return static_cast<int>(accessors_.size()) - 1;
	}

	std::string views() const { return join(views_); }
	std::string accessors() const { return join(accessors_); }
	const std::vector<uint8_t> &bin() const { return bin_; }

private:
	static std::string join(const std::vector<std::string> &items)
	{
		std::string out;
		for(size_t i = 0; i < items.size(); ++i){
			out += (i ? "," : "") + items[i];
		}
		return out;
	}
	std::vector<uint8_t> bin_;
	std::vector<std::string> views_;
	std::vector<std::string> accessors_;
};

std::string number(float v)
{
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%.9g", v);
	return buf;
}

// ---- 読み込み ----

struct FloatArray
{
	std::vector<float> values;
	size_t count = 0;
	int components = 0;
};

} // namespace

bool saveVrma(const std::string &fullPath, const HumanoidAnimation &animation)
{
	// ノード: ヒューマノイドのボーン(restの並び)→表情
	std::map<std::string, int> nodeOf;
	std::vector<std::string> nodeNames;
	for(const auto &r : animation.rest){
		nodeOf[r.bone] = static_cast<int>(nodeNames.size());
		nodeNames.push_back(r.bone);
	}
	const size_t humanoidNodeCount = nodeNames.size();
	for(const auto &e : animation.expressions){
		nodeOf["expression:" + e.name] = static_cast<int>(nodeNames.size());
		nodeNames.push_back(e.name);
	}

	GlbWriter writer;
	std::string samplers, channels;
	int samplerCount = 0;
	auto addChannel = [&](int node, const char *path, const std::vector<float> &times, const std::vector<float> &values, int components, const char *type){
		const int input = writer.addAccessor(times, 1, "SCALAR", true);
		const int output = writer.addAccessor(values, components, type, false);
		samplers += (samplerCount ? "," : "") + std::string("{\"input\":") + std::to_string(input) + ",\"interpolation\":\"LINEAR\",\"output\":" + std::to_string(output) + "}";
		channels += (samplerCount ? "," : "") + std::string("{\"sampler\":") + std::to_string(samplerCount) + ",\"target\":{\"node\":" + std::to_string(node) + ",\"path\":\"" + path + "\"}}";
		++samplerCount;
	};
	for(const auto &track : animation.rotations){
		const auto it = nodeOf.find(track.bone);
		if(it == nodeOf.end() || track.times.empty()){
			continue;
		}
		std::vector<float> values;
		values.reserve(track.values.size() * 4);
		for(const Quat &q : track.values){
			values.insert(values.end(), {q.x, q.y, q.z, q.w});
		}
		addChannel(it->second, "rotation", track.times, values, 4, "VEC4");
	}
	if(!animation.hipsTimes.empty() && nodeOf.count("hips")){
		std::vector<float> values;
		for(const Vec3 &v : animation.hipsTranslations){
			values.insert(values.end(), {v.x, v.y, v.z});
		}
		addChannel(nodeOf["hips"], "translation", animation.hipsTimes, values, 3, "VEC3");
	}
	for(const auto &e : animation.expressions){
		std::vector<float> values;
		for(const float w : e.values){
			values.insert(values.end(), {w, 0.0f, 0.0f}); // 表情の重みは、ノードの移動のxに入れる(VRMAの仕様)
		}
		addChannel(nodeOf["expression:" + e.name], "translation", e.times, values, 3, "VEC3");
	}

	// ノードの木
	std::string nodes;
	std::map<std::string, std::vector<int>> children;
	for(const auto &r : animation.rest){
		if(!r.parent.empty() && nodeOf.count(r.parent)){
			children[r.parent].push_back(nodeOf[r.bone]);
		}
	}
	std::string sceneNodes;
	for(size_t i = 0; i < nodeNames.size(); ++i){
		std::string node = "{\"name\":\"" + nodeNames[i] + "\"";
		if(i < humanoidNodeCount){
			const auto &r = animation.rest[i];
			const Vec3 t = r.parent.empty() ? animation.hipsRest : r.translation;
			node += ",\"translation\":[" + number(t.x) + "," + number(t.y) + "," + number(t.z) + "]";
			const auto kids = children.find(r.bone);
			if(kids != children.end()){
				node += ",\"children\":[";
				for(size_t k = 0; k < kids->second.size(); ++k){
					node += (k ? "," : "") + std::to_string(kids->second[k]);
				}
				node += "]";
			}
			if(r.parent.empty()){
				sceneNodes += (sceneNodes.empty() ? "" : ",") + std::to_string(i);
			}
		}
		else{
			sceneNodes += (sceneNodes.empty() ? "" : ",") + std::to_string(i);
		}
		node += "}";
		nodes += (i ? "," : "") + node;
	}
	std::string humanBones, expressionNodes;
	for(size_t i = 0; i < humanoidNodeCount; ++i){
		humanBones += (i ? "," : "") + std::string("\"") + nodeNames[i] + "\":{\"node\":" + std::to_string(i) + "}";
	}
	for(size_t i = humanoidNodeCount; i < nodeNames.size(); ++i){
		expressionNodes += (i > humanoidNodeCount ? "," : "") + std::string("\"") + nodeNames[i] + "\":{\"node\":" + std::to_string(i) + "}";
	}

	std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"SDL3_Lua5_Vulkan vmd2vrma\"},\"extensionsUsed\":[\"VRMC_vrm_animation\"],"
		"\"extensions\":{\"VRMC_vrm_animation\":{\"specVersion\":\"1.0\",\"humanoid\":{\"humanBones\":{" + humanBones + "}}"
		+ (expressionNodes.empty() ? "" : ",\"expressions\":{\"preset\":{" + expressionNodes + "}}") + "}},"
		"\"scene\":0,\"scenes\":[{\"nodes\":[" + sceneNodes + "]}],\"nodes\":[" + nodes + "],"
		"\"animations\":[{\"name\":\"vrma\",\"samplers\":[" + samplers + "],\"channels\":[" + channels + "]}],"
		"\"accessors\":[" + writer.accessors() + "],\"bufferViews\":[" + writer.views() + "],"
		"\"buffers\":[{\"byteLength\":" + std::to_string(writer.bin().size()) + "}]}";
	while(json.size() % 4 != 0){ json += ' '; }
	std::vector<uint8_t> bin = writer.bin();
	while(bin.size() % 4 != 0){ bin.push_back(0); }

	std::vector<uint8_t> out;
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
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VRMA open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return false;
	}
	const bool ok = SDL_WriteIO(io, out.data(), out.size()) == out.size();
	SDL_CloseIO(io);
	return ok;
}

std::shared_ptr<HumanoidAnimation> loadVrma(const std::string &fullPath)
{
	size_t fileSize = 0;
	void *fileData = SDL_LoadFile(fullPath.c_str(), &fileSize);
	if(!fileData){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VRMA open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return nullptr;
	}
	auto result = std::make_shared<HumanoidAnimation>();
	try{
		const uint8_t *data = static_cast<const uint8_t *>(fileData);
		if(fileSize < 20 || std::memcmp(data, "glTF", 4) != 0){
			throw std::runtime_error("not a glb file");
		}
		const char *jsonText = nullptr;
		size_t jsonLength = 0;
		const uint8_t *bin = nullptr;
		size_t binSize = 0;
		for(size_t offset = 12; offset + 8 <= fileSize;){
			uint32_t length, type;
			std::memcpy(&length, data + offset, 4);
			std::memcpy(&type, data + offset + 4, 4);
			if(offset + 8 + length > fileSize){
				throw std::runtime_error("chunk out of file range");
			}
			if(type == 0x4E4F534A){ jsonText = reinterpret_cast<const char *>(data + offset + 8); jsonLength = length; }
			else if(type == 0x004E4942 && !bin){ bin = data + offset + 8; binSize = length; }
			offset += 8 + static_cast<size_t>(length);
		}
		if(!jsonText || !bin){
			throw std::runtime_error("missing chunk");
		}
		JsonValue json;
		std::string error;
		if(!parseJson(jsonText, jsonLength, json, error)){
			throw std::runtime_error("JSON: " + error);
		}
		const JsonValue &ext = json["extensions"]["VRMC_vrm_animation"];
		if(!ext.isObject()){
			throw std::runtime_error("not a VRMA (no VRMC_vrm_animation)");
		}
		const JsonValue &nodes = json["nodes"];

		auto readAccessor = [&](int index) -> FloatArray {
			const JsonValue &a = json["accessors"][static_cast<size_t>(index)];
			FloatArray out;
			const std::string type = a["type"].asString();
			out.components = type == "SCALAR" ? 1 : (type == "VEC3" ? 3 : (type == "VEC4" ? 4 : 0));
			out.count = static_cast<size_t>(a["count"].asNumber());
			if(out.components == 0 || a["componentType"].asInt() != 5126){
				throw std::runtime_error("unsupported accessor");
			}
			const JsonValue &view = json["bufferViews"][static_cast<size_t>(a["bufferView"].asInt(-1))];
			const size_t start = static_cast<size_t>(view["byteOffset"].asNumber()) + static_cast<size_t>(a["byteOffset"].asNumber());
			const size_t bytes = out.count * static_cast<size_t>(out.components) * sizeof(float);
			if(view.isNull() || start + bytes > binSize){
				throw std::runtime_error("accessor out of buffer range");
			}
			out.values.resize(out.count * static_cast<size_t>(out.components));
			std::memcpy(out.values.data(), bin + start, bytes);
			return out;
		};

		// ヒューマノイドのノード名
		std::map<int, std::string> boneOfNode;
		for(const auto &entry : ext["humanoid"]["humanBones"].object){
			boneOfNode[entry.second["node"].asInt(-1)] = entry.first;
		}
		std::map<int, std::string> expressionOfNode;
		for(const char *group : {"preset", "custom"}){
			for(const auto &entry : ext["expressions"][group].object){
				expressionOfNode[entry.second["node"].asInt(-1)] = entry.first;
			}
		}
		// 腰の休止ポーズでの位置(ノードの移動。親が無い前提)
		for(const auto &entry : boneOfNode){
			if(entry.second == "hips"){
				const JsonValue &t = nodes[static_cast<size_t>(entry.first)]["translation"];
				result->hipsRest = {static_cast<float>(t[size_t(0)].asNumber()), static_cast<float>(t[size_t(1)].asNumber()), static_cast<float>(t[size_t(2)].asNumber())};
			}
		}

		const JsonValue &animation = json["animations"][size_t(0)];
		const JsonValue &samplers = animation["samplers"];
		const JsonValue &channels = animation["channels"];
		for(size_t c = 0; c < channels.size(); ++c){
			const JsonValue &channel = channels[c];
			const int node = channel["target"]["node"].asInt(-1);
			const std::string path = channel["target"]["path"].asString();
			const JsonValue &sampler = samplers[static_cast<size_t>(channel["sampler"].asInt(-1))];
			if(sampler.isNull() || sampler["interpolation"].asString("LINEAR") == "CUBICSPLINE"){
				continue; // 3次スプラインは扱わない
			}
			const FloatArray times = readAccessor(sampler["input"].asInt(-1));
			const FloatArray values = readAccessor(sampler["output"].asInt(-1));
			if(times.count == 0 || values.count != times.count){
				continue;
			}
			result->duration = std::max(result->duration, times.values.back());
			const auto bone = boneOfNode.find(node);
			const auto expression = expressionOfNode.find(node);
			if(bone != boneOfNode.end() && path == "rotation" && values.components == 4){
				HumanoidAnimation::RotationTrack track;
				track.bone = bone->second;
				track.times = times.values;
				for(size_t i = 0; i < values.count; ++i){
					track.values.push_back(Quat{values.values[i * 4], values.values[i * 4 + 1], values.values[i * 4 + 2], values.values[i * 4 + 3]}.normalized());
				}
				result->rotations.push_back(std::move(track));
			}
			else if(bone != boneOfNode.end() && bone->second == "hips" && path == "translation" && values.components == 3){
				result->hipsTimes = times.values;
				for(size_t i = 0; i < values.count; ++i){
					result->hipsTranslations.push_back({values.values[i * 3], values.values[i * 3 + 1], values.values[i * 3 + 2]});
				}
			}
			else if(expression != expressionOfNode.end() && path == "translation" && values.components == 3){
				HumanoidAnimation::ScalarTrack track;
				track.name = expression->second;
				track.times = times.values;
				for(size_t i = 0; i < values.count; ++i){
					track.values.push_back(values.values[i * 3]); // 表情の重み = 移動のx
				}
				result->expressions.push_back(std::move(track));
			}
		}
		if(result->rotations.empty()){
			throw std::runtime_error("no humanoid rotation tracks");
		}

		// VRMAの仕様は「休止ポーズの回転を全部なし(正規化)」だが、そうなっていないファイル(ノードが休止ポーズの回転を持ち、
		// アニメーションの回転がその回転を含む絶対値のもの)もある。その場合は、ノードの階層から、
		// 「休止ポーズからのワールドの回転」を求めて、正規化した局所の回転に直す(一定の間隔で取り直す)
		bool needsRebase = false;
		std::vector<Quat> restRotation(nodes.size());
		for(size_t i = 0; i < nodes.size(); ++i){
			const JsonValue &r = nodes[i]["rotation"];
			if(r.size() == 4){
				restRotation[i] = Quat{static_cast<float>(r[size_t(0)].asNumber()), static_cast<float>(r[size_t(1)].asNumber()),
					static_cast<float>(r[size_t(2)].asNumber()), static_cast<float>(r[size_t(3)].asNumber(1.0))}.normalized();
				if(boneOfNode.count(static_cast<int>(i)) && (std::fabs(restRotation[i].x) > 1e-3f || std::fabs(restRotation[i].y) > 1e-3f || std::fabs(restRotation[i].z) > 1e-3f)){
					needsRebase = true;
				}
			}
		}
		if(needsRebase){
			std::vector<int> parent(nodes.size(), -1);
			for(size_t i = 0; i < nodes.size(); ++i){
				for(size_t k = 0; k < nodes[i]["children"].size(); ++k){
					const int child = nodes[i]["children"][k].asInt(-1);
					if(child >= 0 && static_cast<size_t>(child) < nodes.size()){ parent[static_cast<size_t>(child)] = static_cast<int>(i); }
				}
			}
			// 親が先になる順(深さ優先)
			std::vector<int> order;
			std::vector<bool> seen(nodes.size(), false);
			std::vector<int> stack;
			for(size_t i = nodes.size(); i > 0; --i){ if(parent[i - 1] < 0){ stack.push_back(static_cast<int>(i - 1)); } }
			while(!stack.empty()){
				const int n = stack.back();
				stack.pop_back();
				if(seen[static_cast<size_t>(n)]){ continue; }
				seen[static_cast<size_t>(n)] = true;
				order.push_back(n);
				for(size_t k = nodes[static_cast<size_t>(n)]["children"].size(); k > 0; --k){
					stack.push_back(nodes[static_cast<size_t>(n)]["children"][k - 1].asInt());
				}
			}
			std::vector<Quat> restGlobal(nodes.size());
			for(const int n : order){
				const int p = parent[static_cast<size_t>(n)];
				restGlobal[static_cast<size_t>(n)] = p >= 0 ? (restGlobal[static_cast<size_t>(p)] * restRotation[static_cast<size_t>(n)]).normalized() : restRotation[static_cast<size_t>(n)];
			}
			// ヒューマノイドの親(祖先のうち最初のヒューマノイド)
			std::vector<int> humanoidParent(nodes.size(), -1);
			for(size_t i = 0; i < nodes.size(); ++i){
				int p = parent[i];
				while(p >= 0 && !boneOfNode.count(p)){ p = parent[static_cast<size_t>(p)]; }
				humanoidParent[i] = p;
			}
			std::map<int, size_t> trackOfNode; // ヒューマノイドのノード → result->rotationsの添字
			for(size_t t = 0; t < result->rotations.size(); ++t){
				for(const auto &entry : boneOfNode){
					if(entry.second == result->rotations[t].bone){ trackOfNode[entry.first] = t; }
				}
			}
			const auto oldTracks = result->rotations;
			std::vector<HumanoidAnimation::RotationTrack> tracks;
			std::map<int, size_t> newTrackOfNode;
			for(const auto &entry : trackOfNode){
				newTrackOfNode[entry.first] = tracks.size();
				HumanoidAnimation::RotationTrack track;
				track.bone = oldTracks[entry.second].bone;
				tracks.push_back(std::move(track));
			}
			constexpr float kFps = 30.0f;
			const size_t sampleCount = static_cast<size_t>(std::ceil(result->duration * kFps)) + 1;
			std::vector<Quat> animGlobal(nodes.size()), delta(nodes.size());
			for(size_t s = 0; s < sampleCount; ++s){
				const float time = std::min(static_cast<float>(s) / kFps, result->duration);
				for(const int n : order){
					Quat local = restRotation[static_cast<size_t>(n)];
					const auto it = trackOfNode.find(n);
					if(it != trackOfNode.end()){
						const auto &track = oldTracks[it->second];
						size_t i0, i1;
						float alpha;
						locate(track.times, time, i0, i1, alpha);
						local = i0 == i1 ? track.values[i0] : Quat::slerp(track.values[i0], track.values[i1], alpha);
					}
					const int p = parent[static_cast<size_t>(n)];
					animGlobal[static_cast<size_t>(n)] = p >= 0 ? (animGlobal[static_cast<size_t>(p)] * local).normalized() : local;
					delta[static_cast<size_t>(n)] = (animGlobal[static_cast<size_t>(n)] * restGlobal[static_cast<size_t>(n)].conjugate()).normalized();
				}
				for(const auto &entry : newTrackOfNode){
					const int hp = humanoidParent[static_cast<size_t>(entry.first)];
					const Quat parentDelta = hp >= 0 ? delta[static_cast<size_t>(hp)] : Quat{};
					Quat q = (parentDelta.conjugate() * delta[static_cast<size_t>(entry.first)]).normalized();
					auto &track = tracks[entry.second];
					if(!track.values.empty()){
						const Quat &prev = track.values.back();
						if(prev.x * q.x + prev.y * q.y + prev.z * q.z + prev.w * q.w < 0.0f){ q = {-q.x, -q.y, -q.z, -q.w}; }
					}
					track.times.push_back(time);
					track.values.push_back(q);
				}
			}
			result->rotations = std::move(tracks);
		}
	}
	catch(const std::exception &e){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VRMA parse error: %s (%s)", e.what(), fullPath.c_str());
		result = nullptr;
	}
	SDL_free(fileData);
	return result;
}

// ---- 変換 ----

bool convertVmdToVrma(const ModelData &mmdModel, const Motion &motion, const ModelData &vrmModel, const std::string &outPath, float fps)
{
	Skeleton source(mmdModel.bones);
	Skeleton target(vrmModel.bones);
	auto retargeter = Retargeter::create(source, target, vrmModel.humanoidBones);
	if(!retargeter){
		return false;
	}
	MorphSet mmdMorphs(mmdModel.morphs, mmdModel.vertices.size());
	MotionPlayer player(std::make_shared<const Motion>(motion), source, &mmdMorphs);
	const float *sign = kAnimationAxisSign;

	const size_t boneCount = target.boneCount();
	// ヒューマノイドの親(VRMのボーンの親をたどって、最初に対応づけのあるもの)
	std::vector<int> humanoidParent(boneCount, -1);
	std::vector<bool> isHumanoid(boneCount, false);
	for(size_t i = 0; i < boneCount; ++i){
		isHumanoid[i] = !retargeter->humanoidName(static_cast<int>(i)).empty();
		int p = target.bone(static_cast<int>(i)).parent;
		while(p >= 0 && retargeter->humanoidName(p).empty()){
			p = target.bone(p).parent;
		}
		humanoidParent[i] = p;
	}

	auto animation = std::make_shared<HumanoidAnimation>();
	std::vector<int> trackOfBone(boneCount, -1);
	for(size_t i = 0; i < boneCount; ++i){
		if(!isHumanoid[i]){
			continue;
		}
		HumanoidAnimation::RotationTrack track;
		track.bone = retargeter->humanoidName(static_cast<int>(i));
		trackOfBone[i] = static_cast<int>(animation->rotations.size());
		animation->rotations.push_back(std::move(track));
		// 休止ポーズの階層(glTFの座標)
		HumanoidAnimation::RestNode rest;
		rest.bone = retargeter->humanoidName(static_cast<int>(i));
		const int parent = humanoidParent[i];
		rest.parent = parent >= 0 ? retargeter->humanoidName(parent) : std::string();
		const float *p = target.bone(static_cast<int>(i)).position;
		Vec3 offset = {p[0], p[1], p[2]};
		if(parent >= 0){
			const float *pp = target.bone(parent).position;
			offset = {p[0] - pp[0], p[1] - pp[1], p[2] - pp[2]};
		}
		rest.translation = mirrorPoint(offset, sign);
		if(rest.parent.empty() && rest.bone == "hips"){
			animation->hipsRest = mirrorPoint({p[0], p[1], p[2]}, sign);
		}
		animation->rest.push_back(std::move(rest));
	}

	// 表情: MMDのモーフ名 → VRMの表情名
	static const std::pair<const char *, const char *> kExpressions[] = {
		{"まばたき", "blink"}, {"あ", "aa"}, {"い", "ih"}, {"う", "ou"}, {"え", "ee"}, {"お", "oh"},
		{"笑い", "happy"}, {"怒り", "angry"}, {"困る", "sad"}, {"驚き", "surprised"},
	};
	std::vector<std::pair<int, size_t>> expressionSource; // (MMDのモーフ番号, animation->expressionsの添字)
	for(const auto &entry : kExpressions){
		const int morph = mmdMorphs.findMorph(entry.first);
		if(morph >= 0){
			expressionSource.emplace_back(morph, animation->expressions.size());
			animation->expressions.push_back({entry.second, {}, {}});
		}
	}

	const float lastFrame = static_cast<float>(motion.lastFrame);
	const float step = 30.0f / std::max(fps, 1.0f); // 1サンプルあたりのVMDのフレーム数
	size_t samples = 0;
	for(float frame = 0.0f;; frame += step){
		const float f = std::min(frame, lastFrame);
		const float time = f / 30.0f;
		source.resetPose();
		player.apply(source, f);
		source.update();
		mmdMorphs.resetWeights();
		player.applyMorphs(mmdMorphs, f);

		RetargetPose pose;
		retargeter->computePose(source, target, pose);
		std::vector<Quat> accumulated(boneCount); // ヒューマノイドの親までの、ワールドの回転(正規化した局所の回転の積み上げ)
		for(size_t i = 0; i < boneCount; ++i){
			const int parent = humanoidParent[i];
			const Quat parentD = parent >= 0 ? accumulated[static_cast<size_t>(parent)] : Quat{};
			if(!isHumanoid[i]){
				accumulated[i] = parentD;
				continue;
			}
			accumulated[i] = pose.worldDelta[i];
			const Quat local = (parentD.conjugate() * pose.worldDelta[i]).normalized();
			auto &track = animation->rotations[static_cast<size_t>(trackOfBone[i])];
			Quat q = mirrorQuat(local, sign);
			if(!track.values.empty()){ // 連続するサンプルで、符号が反転して補間が遠回りにならないようにそろえる
				const Quat &prev = track.values.back();
				if(prev.x * q.x + prev.y * q.y + prev.z * q.z + prev.w * q.w < 0.0f){
					q = {-q.x, -q.y, -q.z, -q.w};
				}
			}
			track.times.push_back(time);
			track.values.push_back(q);
		}
		const Vec3 delta = mirrorPoint(pose.hipsTranslation, sign);
		animation->hipsTimes.push_back(time);
		animation->hipsTranslations.push_back({animation->hipsRest.x + delta.x, animation->hipsRest.y + delta.y, animation->hipsRest.z + delta.z});
		for(const auto &entry : expressionSource){
			auto &track = animation->expressions[entry.second];
			track.times.push_back(time);
			track.values.push_back(std::clamp(mmdMorphs.weight(entry.first), 0.0f, 1.0f));
		}
		++samples;
		if(f >= lastFrame){
			animation->duration = time;
			break;
		}
	}
	// 変化のない表情のトラックは省く
	animation->expressions.erase(std::remove_if(animation->expressions.begin(), animation->expressions.end(), [](const HumanoidAnimation::ScalarTrack &t){
		return std::all_of(t.values.begin(), t.values.end(), [](float v){ return v == 0.0f; });
	}), animation->expressions.end());
	SDL_Log("VMD -> VRMA: %zu samples, %zu bone tracks, %zu expression tracks, %.1f s", samples, animation->rotations.size(),
		animation->expressions.size(), animation->duration);
	return saveVrma(outPath, *animation);
}

// ---- 再生 ----

std::unique_ptr<VrmaPlayer> VrmaPlayer::create(const std::shared_ptr<const HumanoidAnimation> &animation, const ModelData &vrmModel,
	const Skeleton &target, const MorphSet *morphs)
{
	std::unordered_map<std::string, int> boneOfName;
	for(const auto &entry : vrmModel.humanoidBones){
		boneOfName.emplace(entry.first, entry.second);
	}
	std::unique_ptr<VrmaPlayer> result(new VrmaPlayer());
	result->animation_ = animation;
	(void)vrmModel;
	for(int k = 0; k < 3; ++k){ result->axisSign_[k] = kAnimationAxisSign[k]; }
	size_t mapped = 0;
	// VRM 0.xの親指の名前: Proximal/Intermediate/Distal(VRM 1.0・VRMAは Metacarpal/Proximal/Distal)。
	// モデルが0.xの名前(ThumbIntermediate)を持つときは、1つずつずらして対応づける
	const bool legacyThumb = boneOfName.count("leftThumbIntermediate") > 0 || boneOfName.count("rightThumbIntermediate") > 0;
	auto lookup = [&](const std::string &name) -> std::unordered_map<std::string, int>::const_iterator {
		if(legacyThumb){
			for(const char *side : {"left", "right"}){
				const std::string prefix = std::string(side) + "Thumb";
				if(name == prefix + "Metacarpal"){ return boneOfName.find(prefix + "Proximal"); }
				if(name == prefix + "Proximal"){ return boneOfName.find(prefix + "Intermediate"); }
			}
		}
		return boneOfName.find(name);
	};
	for(const auto &track : animation->rotations){
		const auto it = lookup(track.bone);
		result->trackBone_.push_back(it == boneOfName.end() ? -1 : it->second);
		mapped += it == boneOfName.end() ? 0 : 1;
	}
	const auto hips = boneOfName.find("hips");
	if(hips != boneOfName.end()){
		result->hipsBone_ = hips->second;
		const float tgt = target.bone(hips->second).position[1];
		result->hipsScale_ = animation->hipsRest.y > 1e-3f && tgt > 1e-3f ? tgt / animation->hipsRest.y : 1.0f;
	}
	for(const auto &track : animation->expressions){
		result->expressionMorph_.push_back(morphs ? morphs->findMorph(track.name) : -1);
	}
	if(mapped < 5 || result->hipsBone_ < 0){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VRMA: could not map the humanoid bones (%zu tracks mapped)", mapped);
		return nullptr;
	}
	return result;
}

void VrmaPlayer::apply(Skeleton &target, MorphSet *morphs, float seconds) const
{
	const size_t boneCount = target.boneCount();
	std::vector<Quat> local(boneCount);
	std::vector<bool> mapped(boneCount, false);
	for(size_t i = 0; i < animation_->rotations.size(); ++i){
		const int bone = trackBone_[i];
		if(bone < 0){
			continue;
		}
		const auto &track = animation_->rotations[i];
		size_t i0, i1;
		float alpha;
		locate(track.times, seconds, i0, i1, alpha);
		const Quat q = i0 == i1 ? track.values[i0] : Quat::slerp(track.values[i0], track.values[i1], alpha);
		local[static_cast<size_t>(bone)] = mirrorQuat(q, axisSign_); // glTFの座標 → このエンジンの座標
		mapped[static_cast<size_t>(bone)] = true;
	}

	// ヒューマノイドの親までの、ワールドの回転の積み上げ(親のボーンが先に並んでいる)
	RetargetPose pose;
	pose.worldDelta.assign(boneCount, Quat{});
	pose.mapped = mapped;
	std::vector<Quat> accumulated(boneCount);
	for(size_t i = 0; i < boneCount; ++i){
		int parent = target.bone(static_cast<int>(i)).parent;
		const Quat parentD = parent >= 0 ? accumulated[static_cast<size_t>(parent)] : Quat{};
		if(!mapped[i]){
			accumulated[i] = parentD; // 対応づけの無いボーンは、親にそのまま従う
			continue;
		}
		accumulated[i] = (parentD * local[i]).normalized();
		pose.worldDelta[i] = accumulated[i];
	}
	pose.hipsBone = hipsBone_;
	if(!animation_->hipsTimes.empty()){
		size_t i0, i1;
		float alpha;
		locate(animation_->hipsTimes, seconds, i0, i1, alpha);
		const Vec3 a = animation_->hipsTranslations[i0], b = animation_->hipsTranslations[i1];
		const Vec3 t = {a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha, a.z + (b.z - a.z) * alpha};
		Vec3 delta = {(t.x - animation_->hipsRest.x) * hipsScale_, (t.y - animation_->hipsRest.y) * hipsScale_, (t.z - animation_->hipsRest.z) * hipsScale_};
		if(inPlace_){
			delta.x = 0.0f;
			delta.z = 0.0f;
		}
		pose.hipsTranslation = mirrorPoint(delta, axisSign_);
	}
	Retargeter::applyPose(pose, target);

	if(morphs){
		for(size_t i = 0; i < animation_->expressions.size(); ++i){
			if(expressionMorph_[i] < 0 || animation_->expressions[i].times.empty()){
				continue;
			}
			const auto &track = animation_->expressions[i];
			size_t i0, i1;
			float alpha;
			locate(track.times, seconds, i0, i1, alpha);
			morphs->setWeight(expressionMorph_[i], track.values[i0] + (track.values[i1] - track.values[i0]) * alpha);
		}
	}
}

} // namespace model
