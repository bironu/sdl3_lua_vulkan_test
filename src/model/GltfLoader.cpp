#include "model/GltfLoader.h"
#include "model/Json.h"
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace model
{

namespace
{

// 4x4行列(列優先、列ベクトルに左から掛ける。glTFと同じ)
struct Mat4
{
	double m[16];
};

Mat4 identity()
{
	Mat4 r{};
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
	return r;
}

Mat4 multiply(const Mat4 &a, const Mat4 &b)
{
	Mat4 r{};
	for(int col = 0; col < 4; ++col){
		for(int row = 0; row < 4; ++row){
			double sum = 0.0;
			for(int k = 0; k < 4; ++k){
				sum += a.m[k * 4 + row] * b.m[col * 4 + k];
			}
			r.m[col * 4 + row] = sum;
		}
	}
	return r;
}

// 平行移動・回転(x,y,z,w)・拡大縮小から T * R * S
Mat4 fromTrs(const double t[3], const double q[4], const double s[3])
{
	const double x = q[0], y = q[1], z = q[2], w = q[3];
	Mat4 r = identity();
	r.m[0] = (1 - 2 * (y * y + z * z)) * s[0];
	r.m[1] = (2 * (x * y + z * w)) * s[0];
	r.m[2] = (2 * (x * z - y * w)) * s[0];
	r.m[4] = (2 * (x * y - z * w)) * s[1];
	r.m[5] = (1 - 2 * (x * x + z * z)) * s[1];
	r.m[6] = (2 * (y * z + x * w)) * s[1];
	r.m[8] = (2 * (x * z + y * w)) * s[2];
	r.m[9] = (2 * (y * z - x * w)) * s[2];
	r.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
	r.m[12] = t[0];
	r.m[13] = t[1];
	r.m[14] = t[2];
	return r;
}

struct Accessor
{
	std::vector<float> values; // 要素数*成分数(正規化整数はfloatに直す)
	int components = 0;
	size_t count = 0;
};

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

class Gltf
{
public:
	Gltf(const uint8_t *bin, size_t binSize, const JsonValue &json) : bin_(bin), binSize_(binSize), json_(json) {}

	// accessorの内容を読む(sparseにも対応)。範囲外などは例外
	Accessor readAccessor(int index) const
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
			readView(a["bufferView"].asInt(), static_cast<size_t>(a["byteOffset"].asNumber()), type, normalized, out.components, out.count, out.values.data(), nullptr);
		}
		const JsonValue &sparse = a["sparse"];
		if(sparse.isObject()){
			const size_t n = static_cast<size_t>(sparse["count"].asNumber());
			std::vector<float> indices(n);
			const JsonValue &idx = sparse["indices"];
			readView(idx["bufferView"].asInt(), static_cast<size_t>(idx["byteOffset"].asNumber()), idx["componentType"].asInt(), false, 1, n, indices.data(), nullptr);
			std::vector<float> values(n * static_cast<size_t>(out.components));
			const JsonValue &val = sparse["values"];
			readView(val["bufferView"].asInt(), static_cast<size_t>(val["byteOffset"].asNumber()), type, normalized, out.components, n, values.data(), nullptr);
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
	void readView(int view, size_t byteOffset, int type, bool normalized, int components, size_t count, float *out, void *) const
	{
		const JsonValue &v = json_["bufferViews"][static_cast<size_t>(view)];
		if(v.isNull()){
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

	// bufferViewの生のバイト列
	bool viewBytes(int view, std::vector<uint8_t> &out) const
	{
		const JsonValue &v = json_["bufferViews"][static_cast<size_t>(view)];
		if(v.isNull()){
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

private:
	const uint8_t *bin_;
	size_t binSize_;
	const JsonValue &json_;
};

// 座標系の変換(右手系→PMXと同じ左手系): 各軸に符号を掛ける
struct Axes
{
	double s[3];
	void point(double *v) const { v[0] *= s[0]; v[1] *= s[1]; v[2] *= s[2]; }
	// 回転(x,y,z,w)を、鏡像の座標系での回転に直す(鏡像では回転軸=擬ベクトルの成分が、他の2軸の符号の積で変わる)
	void quaternion(double *q) const
	{
		const double x = q[0], y = q[1], z = q[2];
		q[0] = s[1] * s[2] * x;
		q[1] = s[0] * s[2] * y;
		q[2] = s[0] * s[1] * z;
	}
};

} // namespace

std::shared_ptr<ModelData> loadVrm(const std::string &fullPath)
{
	size_t fileSize = 0;
	void *fileData = SDL_LoadFile(fullPath.c_str(), &fileSize);
	if(!fileData){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VRM open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return nullptr;
	}

	auto result = std::make_shared<ModelData>();
	try{
		const uint8_t *data = static_cast<const uint8_t *>(fileData);
		if(fileSize < 20 || std::memcmp(data, "glTF", 4) != 0){
			throw std::runtime_error("not a glb file");
		}
		uint32_t version;
		std::memcpy(&version, data + 4, 4);
		if(version != 2){
			throw std::runtime_error("unsupported glTF version");
		}
		// チャンク: JSON と BIN
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
			if(type == 0x4E4F534A){ // "JSON"
				jsonText = reinterpret_cast<const char *>(data + offset + 8);
				jsonLength = length;
			}
			else if(type == 0x004E4942 && !bin){ // "BIN\0"
				bin = data + offset + 8;
				binSize = length;
			}
			offset += 8 + static_cast<size_t>(length);
		}
		if(!jsonText){
			throw std::runtime_error("no JSON chunk");
		}
		JsonValue json;
		std::string error;
		if(!parseJson(jsonText, jsonLength, json, error)){
			throw std::runtime_error("JSON: " + error);
		}
		if(!bin){
			throw std::runtime_error("no BIN chunk");
		}
		const Gltf gltf(bin, binSize, json);

		// VRM 0.x は正面が-Z(Y軸まわりに180度回して、さらにZを鏡像にする=Xの鏡像)、VRM 1.0 とただのglTFは正面が+Z(Zの鏡像)
		const JsonValue &extensions = json["extensions"];
		const bool vrm0 = extensions["VRM"].isObject();
		const Axes axes = vrm0 ? Axes{{-1.0, 1.0, 1.0}} : Axes{{1.0, 1.0, -1.0}};
		for(int k = 0; k < 3; ++k){ result->importAxisSign[k] = static_cast<float>(axes.s[k]); }

		const JsonValue &nodes = json["nodes"];
		const size_t nodeCount = nodes.size();
		if(nodeCount == 0){
			throw std::runtime_error("no nodes");
		}

		// ノードの階層(親)と、深さ優先の順(親が必ず先)
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
		std::vector<int> order;
		{
			std::vector<int> stack;
			const JsonValue &scenes = json["scenes"];
			const JsonValue &sceneNodes = scenes[static_cast<size_t>(json["scene"].asInt(0))]["nodes"];
			for(size_t k = sceneNodes.size(); k > 0; --k){
				stack.push_back(sceneNodes[k - 1].asInt());
			}
			if(stack.empty()){ // シーンの指定が無ければ、親の無いノードすべて
				for(size_t i = nodeCount; i > 0; --i){
					if(parent[i - 1] < 0){ stack.push_back(static_cast<int>(i - 1)); }
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
		}

		// ノードのグローバル行列(休止ポーズ)と、ボーン
		std::vector<Mat4> global(nodeCount, identity());
		std::vector<int> nodeToBone(nodeCount, -1);
		for(const int n : order){
			const JsonValue &node = nodes[static_cast<size_t>(n)];
			double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
			for(int k = 0; k < 3; ++k){
				t[k] = node["translation"][static_cast<size_t>(k)].asNumber(t[k]);
				s[k] = node["scale"][static_cast<size_t>(k)].asNumber(s[k]);
			}
			for(int k = 0; k < 4; ++k){
				q[k] = node["rotation"][static_cast<size_t>(k)].asNumber(q[k]);
			}
			Mat4 local = fromTrs(t, q, s);
			if(node["matrix"].isArray() && node["matrix"].size() == 16){
				for(size_t k = 0; k < 16; ++k){ local.m[k] = node["matrix"][k].asNumber(); }
				q[0] = q[1] = q[2] = 0.0; q[3] = 1.0; // 行列で指定されたノードの回転は分解しない(回転なしとして扱う)
			}
			const int p = parent[static_cast<size_t>(n)];
			global[static_cast<size_t>(n)] = p >= 0 ? multiply(global[static_cast<size_t>(p)], local) : local;

			ModelBone bone;
			bone.name = node["name"].asString("node" + std::to_string(n));
			double position[3] = {global[static_cast<size_t>(n)].m[12], global[static_cast<size_t>(n)].m[13], global[static_cast<size_t>(n)].m[14]};
			axes.point(position);
			for(int k = 0; k < 3; ++k){ bone.position[k] = static_cast<float>(position[k]); }
			axes.quaternion(q);
			for(int k = 0; k < 4; ++k){ bone.restRotation[k] = static_cast<float>(q[k]); }
			bone.parent = p >= 0 ? nodeToBone[static_cast<size_t>(p)] : -1;
			bone.flags = ModelBone::Rotatable | ModelBone::Translatable | ModelBone::Visible | ModelBone::Operable;
			nodeToBone[static_cast<size_t>(n)] = static_cast<int>(result->bones.size());
			result->bones.push_back(std::move(bone));
		}

		// 画像(埋め込みのバイト列をそのまま持つ)
		const JsonValue &images = json["images"];
		result->texturePaths.assign(images.size(), std::string());
		result->embeddedImages.assign(images.size(), {});
		for(size_t i = 0; i < images.size(); ++i){
			if(images[i]["bufferView"].isNumber()){
				gltf.viewBytes(images[i]["bufferView"].asInt(), result->embeddedImages[i]);
			}
		}

		// メッシュを持つノード(同じメッシュを使うノードは最初のものを使う)
		const JsonValue &meshes = json["meshes"];
		std::vector<int> meshNode(meshes.size(), -1);
		for(const int n : order){
			const int mesh = nodes[static_cast<size_t>(n)]["mesh"].asInt(-1);
			if(mesh >= 0 && static_cast<size_t>(mesh) < meshes.size() && meshNode[static_cast<size_t>(mesh)] < 0){
				meshNode[static_cast<size_t>(mesh)] = n;
			}
		}

		std::vector<int> morphBase(meshes.size(), -1); // メッシュのモーフターゲットの先頭が、result->morphsの何番か
		for(size_t m = 0; m < meshes.size(); ++m){
			const int node = meshNode[m];
			if(node < 0){
				continue;
			}
			const Mat4 &g = global[static_cast<size_t>(node)];
			const JsonValue &skin = json["skins"][static_cast<size_t>(nodes[static_cast<size_t>(node)]["skin"].asInt(-1))];
			const JsonValue &joints = skin["joints"];

			const JsonValue &primitives = meshes[m]["primitives"];
			for(size_t pi = 0; pi < primitives.size(); ++pi){
				const JsonValue &prim = primitives[pi];
				if(prim["mode"].asInt(4) != 4 || !prim["attributes"]["POSITION"].isNumber()){
					continue; // 三角形リスト以外は扱わない
				}
				const JsonValue &attr = prim["attributes"];
				const Accessor position = gltf.readAccessor(attr["POSITION"].asInt());
				const size_t vertexCount = position.count;
				Accessor normal, uv, jointIds, weights;
				if(attr["NORMAL"].isNumber()){ normal = gltf.readAccessor(attr["NORMAL"].asInt()); }
				if(attr["TEXCOORD_0"].isNumber()){ uv = gltf.readAccessor(attr["TEXCOORD_0"].asInt()); }
				if(attr["JOINTS_0"].isNumber()){ jointIds = gltf.readAccessor(attr["JOINTS_0"].asInt()); }
				if(attr["WEIGHTS_0"].isNumber()){ weights = gltf.readAccessor(attr["WEIGHTS_0"].asInt()); }

				const uint32_t base = static_cast<uint32_t>(result->vertices.size());
				for(size_t v = 0; v < vertexCount; ++v){
					ModelVertex out{};
					// 位置・法線: メッシュのノードの行列で休止ポーズのモデル空間へ → 座標系の変換
					const double px = position.values[v * 3], py = position.values[v * 3 + 1], pz = position.values[v * 3 + 2];
					double p[3] = {g.m[0] * px + g.m[4] * py + g.m[8] * pz + g.m[12], g.m[1] * px + g.m[5] * py + g.m[9] * pz + g.m[13],
						g.m[2] * px + g.m[6] * py + g.m[10] * pz + g.m[14]};
					axes.point(p);
					for(int k = 0; k < 3; ++k){ out.position[k] = static_cast<float>(p[k]); }
					if(normal.count > v){
						const double nx = normal.values[v * 3], ny = normal.values[v * 3 + 1], nz = normal.values[v * 3 + 2];
						double n[3] = {g.m[0] * nx + g.m[4] * ny + g.m[8] * nz, g.m[1] * nx + g.m[5] * ny + g.m[9] * nz, g.m[2] * nx + g.m[6] * ny + g.m[10] * nz};
						const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
						if(len > 1e-9){ n[0] /= len; n[1] /= len; n[2] /= len; }
						axes.point(n);
						for(int k = 0; k < 3; ++k){ out.normal[k] = static_cast<float>(n[k]); }
					}
					else{
						out.normal[1] = 1.0f;
					}
					if(uv.count > v){
						out.uv[0] = uv.values[v * 2];
						out.uv[1] = uv.values[v * 2 + 1];
					}
					if(jointIds.count > v && weights.count > v){
						float total = 0.0f;
						for(int k = 0; k < 4; ++k){
							const size_t j = static_cast<size_t>(jointIds.values[v * 4 + static_cast<size_t>(k)]);
							const float w = weights.values[v * 4 + static_cast<size_t>(k)];
							const int joint = joints[j].asInt(-1);
							if(w > 0.0f && joint >= 0 && static_cast<size_t>(joint) < nodeCount){
								out.bones[k] = nodeToBone[static_cast<size_t>(joint)];
								out.weights[k] = w;
								total += w;
							}
						}
						if(total > 0.0f){
							for(float &w : out.weights){ w /= total; }
						}
					}
					else{
						// スキンの無いメッシュは、そのメッシュのノードのボーンに固定する
						out.bones[0] = nodeToBone[static_cast<size_t>(node)];
						out.weights[0] = 1.0f;
					}
					result->vertices.push_back(out);
				}

				// 面: 三角形の頂点順を入れ替える(座標系の鏡像で見かけの向きが逆になる分。VulkanModelがPMXと同じ扱いで戻す)
				ModelMaterial material;
				material.firstIndex = static_cast<uint32_t>(result->indices.size());
				if(prim["indices"].isNumber()){
					const Accessor indices = gltf.readAccessor(prim["indices"].asInt());
					for(size_t i = 0; i + 2 < indices.count; i += 3){
						const uint32_t a = static_cast<uint32_t>(indices.values[i]), b = static_cast<uint32_t>(indices.values[i + 1]), c = static_cast<uint32_t>(indices.values[i + 2]);
						if(a >= vertexCount || b >= vertexCount || c >= vertexCount){
							throw std::runtime_error("index out of range");
						}
						result->indices.push_back(base + a);
						result->indices.push_back(base + c);
						result->indices.push_back(base + b);
					}
				}
				else{
					for(size_t i = 0; i + 2 < vertexCount; i += 3){
						result->indices.push_back(base + static_cast<uint32_t>(i));
						result->indices.push_back(base + static_cast<uint32_t>(i + 2));
						result->indices.push_back(base + static_cast<uint32_t>(i + 1));
					}
				}
				material.indexCount = static_cast<uint32_t>(result->indices.size()) - material.firstIndex;

				// 材質(基本色とテクスチャ)
				const JsonValue &mat = json["materials"][static_cast<size_t>(prim["material"].asInt(-1))];
				const JsonValue &pbr = mat["pbrMetallicRoughness"];
				material.name = mat["name"].asString("material");
				for(int k = 0; k < 4; ++k){
					material.diffuse[k] = static_cast<float>(pbr["baseColorFactor"][static_cast<size_t>(k)].asNumber(1.0));
				}
				const std::string alphaMode = mat["alphaMode"].asString("OPAQUE");
				if(alphaMode == "OPAQUE"){
					material.diffuse[3] = 1.0f;
					material.ignoreTextureAlpha = true;
				}
				else if(alphaMode == "MASK"){
					material.alphaMask = true;
					material.alphaCutoff = static_cast<float>(mat["alphaCutoff"].asNumber(0.5));
				}
				// MToon(VRoidなどは、互換用にKHR_materials_unlitも付けるが、MToonがあればMToonを使う)
				const JsonValue &mtoon = mat["extensions"]["VRMC_materials_mtoon"];
				if(mtoon.isObject()){
					ModelMaterial::MToon &m = material.mtoon;
					m.enabled = true;
					{ std::string upper = material.name; std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); }); m.skin = upper.find("SKIN") != std::string::npos || upper.find("FACEMOUTH") != std::string::npos; /* VRoidの顔+口の材質は、顔の肌でもある */ m.faceSkin = m.skin && upper.find("FACE") != std::string::npos; }
					for(size_t k = 0; k < 3; ++k){
						m.shadeColor[k] = static_cast<float>(mtoon["shadeColorFactor"][k].asNumber(1.0));
						m.rimColor[k] = static_cast<float>(mtoon["parametricRimColorFactor"][k].asNumber(0.0));
						m.outlineColor[k] = static_cast<float>(mtoon["outlineColorFactor"][k].asNumber(0.0));
						m.emissive[k] = static_cast<float>(mat["emissiveFactor"][k].asNumber(0.0)); // 発光は「係数 x テクスチャ」
					}
					// テクスチャ(影の色・発光): glTFのtexture番号→画像の番号
					auto imageOf = [&](const JsonValue &textureInfo){
						const int t = textureInfo["index"].asInt(-1);
						const int image = t >= 0 ? json["textures"][static_cast<size_t>(t)]["source"].asInt(-1) : -1;
						return image >= 0 && image < static_cast<int>(images.size()) ? image : -1;
					};
					m.shadeTexture = imageOf(mtoon["shadeMultiplyTexture"]);
					m.emissiveTexture = imageOf(mat["emissiveTexture"]);
					m.shadingShift = static_cast<float>(mtoon["shadingShiftFactor"].asNumber(0.0));
					m.shadingToony = static_cast<float>(mtoon["shadingToonyFactor"].asNumber(0.9));
					m.giEqualization = static_cast<float>(mtoon["giEqualizationFactor"].asNumber(0.9));
					m.rimLightingMix = static_cast<float>(mtoon["rimLightingMixFactor"].asNumber(1.0));
					m.rimFresnelPower = static_cast<float>(mtoon["parametricRimFresnelPowerFactor"].asNumber(5.0));
					m.rimLift = static_cast<float>(mtoon["parametricRimLiftFactor"].asNumber(0.0));
					m.outlineLightingMix = static_cast<float>(mtoon["outlineLightingMixFactor"].asNumber(1.0));
					const std::string outlineMode = mtoon["outlineWidthMode"].asString("none");
					m.outlineWidth = static_cast<float>(mtoon["outlineWidthFactor"].asNumber(0.0));
					// 画面座標の太さ(screenCoordinates)も、ワールド座標のものとして近似する
					m.outlineMode = (outlineMode == "none" || m.outlineWidth <= 0.0f) ? 2 : 1;
				}
				// VRM 0.x のMToon(extensions.VRM.materialProperties[材質番号]。shaderが"VRM/MToon")。VRM 1.0 のMToonへ換算して同じ形で持つ
				else{
					const int materialIndex = prim["material"].asInt(-1);
					const JsonValue &props = extensions["VRM"]["materialProperties"][static_cast<size_t>(std::max(materialIndex, 0))];
					if(materialIndex >= 0 && props.isObject() && props["shader"].asString() == "VRM/MToon"){
						ModelMaterial::MToon &m = material.mtoon;
						m.enabled = true;
						{ std::string upper = material.name; std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); }); m.skin = upper.find("SKIN") != std::string::npos || upper.find("FACEMOUTH") != std::string::npos; m.faceSkin = m.skin && upper.find("FACE") != std::string::npos; }
						const JsonValue &floats = props["floatProperties"];
						const JsonValue &vectors = props["vectorProperties"];
						const JsonValue &textures = props["textureProperties"];
						const auto number = [&](const char *key, double fallback){ return static_cast<float>(floats[key].asNumber(fallback)); };
						for(size_t k = 0; k < 3; ++k){
							m.shadeColor[k] = static_cast<float>(vectors["_ShadeColor"][k].asNumber(1.0));
							m.rimColor[k] = static_cast<float>(vectors["_RimColor"][k].asNumber(0.0));
							m.outlineColor[k] = static_cast<float>(vectors["_OutlineColor"][k].asNumber(0.0));
							m.emissive[k] = static_cast<float>(vectors["_EmissionColor"][k].asNumber(0.0));
						}
						const auto imageOfTexture = [&](const char *key){
							const int t = textures[key].asInt(-1);
							const int image = t >= 0 ? json["textures"][static_cast<size_t>(t)]["source"].asInt(-1) : -1;
							return image >= 0 && image < static_cast<int>(images.size()) ? image : -1;
						};
						m.shadeTexture = imageOfTexture("_ShadeTexture");
						m.emissiveTexture = imageOfTexture("_EmissionMap");
						// 陰の境目: v0は t = linearstep(S, S + (1-T), 0.5*N・L + 0.5)、v1は t = linearstep(-1+T', 1-T', N・L + shift)。
						// 2つの式が一致するのは T' = T、shift = T - 2*S のとき(VRoidがv1で書き出す値と同じ関係)
						const float shadeShift = number("_ShadeShift", 0.0), shadeToony = number("_ShadeToony", 0.9);
						m.shadingToony = shadeToony;
						m.shadingShift = shadeToony - 2.0f * shadeShift;
						m.giEqualization = 1.0f - number("_IndirectLightIntensity", 0.1);
						m.rimLightingMix = number("_RimLightingMix", 0.0);
						m.rimFresnelPower = number("_RimFresnelPower", 1.0);
						m.rimLift = number("_RimLift", 0.0);
						m.outlineLightingMix = number("_OutlineLightingMix", 1.0);
						// 輪郭線: _OutlineWidthMode 0=なし、1=ワールド座標、2=画面座標(ワールド座標の太さとして近似)。太さはセンチメートル単位(x0.01でメートル)
						m.outlineWidth = number("_OutlineWidth", 0.0) * 0.01f;
						m.outlineMode = (static_cast<int>(number("_OutlineWidthMode", 0.0) + 0.5) == 0 || m.outlineWidth <= 0.0f) ? 2 : 1;
					}
				}
				material.specular = 0.1f;
				material.shininess = 20.0f;
				material.doubleSided = mat["doubleSided"].asBool();
				const int texture = pbr["baseColorTexture"]["index"].asInt(-1);
				if(texture >= 0){
					material.texture = json["textures"][static_cast<size_t>(texture)]["source"].asInt(-1);
					if(material.texture >= static_cast<int>(images.size())){
						material.texture = -1;
					}
				}
				result->materials.push_back(std::move(material));

				// モーフターゲット(頂点の位置の差分)。メッシュ内のプリミティブは同じターゲットの並びを持つ
				const JsonValue &targets = prim["targets"];
				if(targets.size() > 0){
					if(morphBase[m] < 0){
						morphBase[m] = static_cast<int>(result->morphs.size());
						const JsonValue &names = meshes[m]["extras"]["targetNames"];
						for(size_t t = 0; t < targets.size(); ++t){
							ModelMorph morph;
							morph.type = ModelMorph::Type::Vertex;
							morph.name = names[t].asString(meshes[m]["name"].asString("mesh") + "_target" + std::to_string(t));
							result->morphs.push_back(std::move(morph));
						}
					}
					for(size_t t = 0; t < targets.size() && static_cast<size_t>(morphBase[m]) + t < result->morphs.size(); ++t){
						if(!targets[t]["POSITION"].isNumber()){
							continue;
						}
						const Accessor delta = gltf.readAccessor(targets[t]["POSITION"].asInt());
						ModelMorph &morph = result->morphs[static_cast<size_t>(morphBase[m]) + t];
						for(size_t v = 0; v < delta.count && v < vertexCount; ++v){
							const double dx = delta.values[v * 3], dy = delta.values[v * 3 + 1], dz = delta.values[v * 3 + 2];
							if(dx == 0.0 && dy == 0.0 && dz == 0.0){
								continue;
							}
							double d[3] = {g.m[0] * dx + g.m[4] * dy + g.m[8] * dz, g.m[1] * dx + g.m[5] * dy + g.m[9] * dz, g.m[2] * dx + g.m[6] * dy + g.m[10] * dz};
							axes.point(d);
							ModelMorph::VertexOffset offset;
							offset.vertex = base + static_cast<uint32_t>(v);
							for(int k = 0; k < 3; ++k){ offset.delta[k] = static_cast<float>(d[k]); }
							morph.vertexOffsets.push_back(offset);
						}
					}
				}
			}
		}

		// 表情: モーフターゲットをまとめたグループモーフにする
		auto addExpression = [&](const std::string &name, int meshIndex, int target, float weight, int existing){
			if(meshIndex < 0 || static_cast<size_t>(meshIndex) >= meshes.size() || morphBase[static_cast<size_t>(meshIndex)] < 0){
				return existing;
			}
			if(existing < 0){
				ModelMorph morph;
				morph.type = ModelMorph::Type::Group;
				morph.name = name;
				result->morphs.push_back(std::move(morph));
				existing = static_cast<int>(result->morphs.size()) - 1;
			}
			ModelMorph::GroupOffset g;
			g.morph = morphBase[static_cast<size_t>(meshIndex)] + target;
			g.rate = weight;
			result->morphs[static_cast<size_t>(existing)].groupOffsets.push_back(g);
			return existing;
		};
		const JsonValue &vrm1 = extensions["VRMC_vrm"];
		if(vrm1.isObject()){
			result->name = vrm1["meta"]["name"].asString();
			for(const char *section : {"preset", "custom"}){
				for(const auto &entry : vrm1["expressions"][section].object){
					int morph = -1;
					const JsonValue &binds = entry.second["morphTargetBinds"];
					for(size_t b = 0; b < binds.size(); ++b){
						const int node = binds[b]["node"].asInt(-1);
						const int mesh = node >= 0 && static_cast<size_t>(node) < nodeCount ? nodes[static_cast<size_t>(node)]["mesh"].asInt(-1) : -1;
						morph = addExpression(entry.first, mesh, binds[b]["index"].asInt(), static_cast<float>(binds[b]["weight"].asNumber(1.0)), morph);
					}
				}
			}
		}
		else if(vrm0){
			result->name = extensions["VRM"]["meta"]["title"].asString();
			const JsonValue &groups = extensions["VRM"]["blendShapeMaster"]["blendShapeGroups"];
			for(size_t gi = 0; gi < groups.size(); ++gi){
				std::string name = groups[gi]["presetName"].asString();
				if(name.empty() || name == "unknown"){
					name = groups[gi]["name"].asString();
				}
				int morph = -1;
				const JsonValue &binds = groups[gi]["binds"];
				for(size_t b = 0; b < binds.size(); ++b){
					morph = addExpression(name, binds[b]["mesh"].asInt(-1), binds[b]["index"].asInt(), static_cast<float>(binds[b]["weight"].asNumber(100.0) / 100.0), morph);
				}
			}
		}
		// ヒューマノイドのボーン(VRM 1.0: humanBones.<名前>.node、VRM 0.x: humanBones[{bone, node}])
		auto addHumanoid = [&](const std::string &name, int node){
			if(node >= 0 && static_cast<size_t>(node) < nodeCount && nodeToBone[static_cast<size_t>(node)] >= 0){
				result->humanoidBones.emplace_back(name, nodeToBone[static_cast<size_t>(node)]);
			}
		};
		if(vrm1.isObject()){
			for(const auto &entry : vrm1["humanoid"]["humanBones"].object){
				addHumanoid(entry.first, entry.second["node"].asInt(-1));
			}
		}
		else if(vrm0){
			const JsonValue &humanBones = extensions["VRM"]["humanoid"]["humanBones"];
			for(size_t k = 0; k < humanBones.size(); ++k){
				addHumanoid(humanBones[k]["bone"].asString(), humanBones[k]["node"].asInt(-1));
			}
		}
		// スプリングボーン(VRM 1.0。0.xのsecondaryAnimationは読まない)
		const JsonValue &springBone = extensions["VRMC_springBone"];
		if(springBone.isObject()){
			const JsonValue &colliders = springBone["colliders"];
			for(size_t k = 0; k < colliders.size(); ++k){
				SpringCollider collider;
				const int node = colliders[k]["node"].asInt(-1);
				collider.bone = node >= 0 && static_cast<size_t>(node) < nodeCount ? nodeToBone[static_cast<size_t>(node)] : -1;
				const JsonValue &shape = colliders[k]["shape"];
				const bool capsule = shape["capsule"].isObject();
				const JsonValue &body = capsule ? shape["capsule"] : shape["sphere"];
				double offset[3], tail[3];
				for(size_t c = 0; c < 3; ++c){
					offset[c] = body["offset"][c].asNumber();
					tail[c] = body["tail"][c].asNumber();
				}
				axes.point(offset);
				axes.point(tail);
				for(int c = 0; c < 3; ++c){
					collider.offset[c] = static_cast<float>(offset[c]);
					collider.tail[c] = static_cast<float>(tail[c]);
				}
				collider.radius = static_cast<float>(body["radius"].asNumber());
				collider.shape = capsule ? SpringCollider::Shape::Capsule : SpringCollider::Shape::Sphere;
				result->springColliders.push_back(collider);
			}
			const JsonValue &groups = springBone["colliderGroups"];
			for(size_t k = 0; k < groups.size(); ++k){
				SpringColliderGroup group;
				for(size_t c = 0; c < groups[k]["colliders"].size(); ++c){
					group.colliders.push_back(groups[k]["colliders"][c].asInt(-1));
				}
				result->springColliderGroups.push_back(std::move(group));
			}
			const JsonValue &springs = springBone["springs"];
			for(size_t k = 0; k < springs.size(); ++k){
				SpringChain chain;
				chain.name = springs[k]["name"].asString();
				for(size_t c = 0; c < springs[k]["colliderGroups"].size(); ++c){
					chain.colliderGroups.push_back(springs[k]["colliderGroups"][c].asInt(-1));
				}
				const JsonValue &joints = springs[k]["joints"];
				for(size_t j = 0; j < joints.size(); ++j){
					SpringJoint joint;
					const int node = joints[j]["node"].asInt(-1);
					joint.bone = node >= 0 && static_cast<size_t>(node) < nodeCount ? nodeToBone[static_cast<size_t>(node)] : -1;
					if(joint.bone < 0){
						continue;
					}
					joint.hitRadius = static_cast<float>(joints[j]["hitRadius"].asNumber(0.0));
					joint.stiffness = static_cast<float>(joints[j]["stiffness"].asNumber(1.0));
					joint.gravityPower = static_cast<float>(joints[j]["gravityPower"].asNumber(0.0));
					joint.dragForce = static_cast<float>(joints[j]["dragForce"].asNumber(0.5));
					double gravity[3] = {joints[j]["gravityDir"][size_t(0)].asNumber(0.0), joints[j]["gravityDir"][size_t(1)].asNumber(-1.0), joints[j]["gravityDir"][size_t(2)].asNumber(0.0)};
					axes.point(gravity);
					for(int c = 0; c < 3; ++c){ joint.gravityDir[c] = static_cast<float>(gravity[c]); }
					chain.joints.push_back(joint);
				}
				if(chain.joints.size() >= 2){
					result->springChains.push_back(std::move(chain));
				}
			}
		}
		// スプリングボーン(VRM 0.x: VRM.secondaryAnimation。boneGroupsの各ボーンの子孫すべてが、揺れるジョイントになる)
		const JsonValue &secondary = extensions["VRM"]["secondaryAnimation"];
		if(vrm0 && secondary.isObject() && result->springChains.empty()){
			const JsonValue &colliderGroups = secondary["colliderGroups"];
			for(size_t k = 0; k < colliderGroups.size(); ++k){
				SpringColliderGroup group;
				const int node = colliderGroups[k]["node"].asInt(-1);
				const int bone = node >= 0 && static_cast<size_t>(node) < nodeCount ? nodeToBone[static_cast<size_t>(node)] : -1;
				const JsonValue &spheres = colliderGroups[k]["colliders"];
				for(size_t c = 0; c < spheres.size(); ++c){
					SpringCollider collider;
					collider.bone = bone;
					collider.shape = SpringCollider::Shape::Sphere;
					double offset[3] = {spheres[c]["offset"]["x"].asNumber(), spheres[c]["offset"]["y"].asNumber(), spheres[c]["offset"]["z"].asNumber()};
					axes.point(offset);
					for(int a = 0; a < 3; ++a){ collider.offset[a] = static_cast<float>(offset[a]); }
					collider.radius = static_cast<float>(spheres[c]["radius"].asNumber());
					group.colliders.push_back(static_cast<int>(result->springColliders.size()));
					result->springColliders.push_back(collider);
				}
				result->springColliderGroups.push_back(std::move(group));
			}
			const JsonValue &boneGroups = secondary["boneGroups"];
			for(size_t k = 0; k < boneGroups.size(); ++k){
				const JsonValue &bg = boneGroups[k];
				SpringJoint setting;
				setting.hitRadius = static_cast<float>(bg["hitRadius"].asNumber(0.02));
				setting.stiffness = static_cast<float>(bg["stiffiness"].asNumber(bg["stiffness"].asNumber(1.0))); // 綴りは仕様のまま(stiffiness)
				setting.gravityPower = static_cast<float>(bg["gravityPower"].asNumber(0.0));
				setting.dragForce = static_cast<float>(bg["dragForce"].asNumber(0.4));
				double gravity[3] = {bg["gravityDir"]["x"].asNumber(0.0), bg["gravityDir"]["y"].asNumber(-1.0), bg["gravityDir"]["z"].asNumber(0.0)};
				axes.point(gravity);
				for(int a = 0; a < 3; ++a){ setting.gravityDir[a] = static_cast<float>(gravity[a]); }
				std::vector<int> groups;
				for(size_t c = 0; c < bg["colliderGroups"].size(); ++c){ groups.push_back(bg["colliderGroups"][c].asInt(-1)); }

				// 根のボーンごとに、子孫を鎖にする。最初の子で鎖を続け、他の子は、別の鎖として後で作る(親が先に処理されるので、動いた親に従う)
				std::vector<int> pending;
				for(size_t r = 0; r < bg["bones"].size(); ++r){ pending.push_back(bg["bones"][r].asInt(-1)); }
				for(size_t head = 0; head < pending.size(); ++head){
					int node = pending[head];
					SpringChain chain;
					chain.name = "secondary" + std::to_string(k);
					chain.colliderGroups = groups;
					chain.virtualTail = true; // 最後のボーンも動かす(先端は仮想)
					while(node >= 0 && static_cast<size_t>(node) < nodeCount && nodeToBone[static_cast<size_t>(node)] >= 0){
						SpringJoint joint = setting;
						joint.bone = nodeToBone[static_cast<size_t>(node)];
						chain.joints.push_back(joint);
						const JsonValue &children = nodes[static_cast<size_t>(node)]["children"];
						int next = -1;
						for(size_t c = 0; c < children.size(); ++c){
							const int child = children[c].asInt(-1);
							if(child < 0 || static_cast<size_t>(child) >= nodeCount || nodeToBone[static_cast<size_t>(child)] < 0){
								continue;
							}
							if(next < 0){ next = child; } else { pending.push_back(child); }
						}
						node = next;
					}
					if(!chain.joints.empty()){
						result->springChains.push_back(std::move(chain));
					}
				}
			}
		}
		if(result->name.empty()){
			const auto slash = fullPath.find_last_of('/');
			result->name = slash == std::string::npos ? fullPath : fullPath.substr(slash + 1);
		}
		if(result->vertices.empty() || result->indices.empty()){
			throw std::runtime_error("no triangles");
		}
	}
	catch(const std::exception &e){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VRM parse error: %s (%s)", e.what(), fullPath.c_str());
		result = nullptr;
	}
	SDL_free(fileData);
	return result;
}

} // namespace model
