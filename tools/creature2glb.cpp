// 丸い生き物の敵(model::buildCreature の形と、game::CreatureAnimator の動き)を、glTF 2.0 のバイナリ(.glb)に書き出すツール。ウィンドウは作らない。
// 使い方: creature2glb [出力.glb] [種類の名前]
//   出力の既定は res/model/enemy/creature.glb。種類の既定は res/lua/data/enemies.lua の、最初の creature の種類。パスはリポジトリ直下からの相対パス。
//   書き出した後、エンジンの glTF の読み込み(model::loadVrm)で読み直して、頂点・ボーンが元と一致するかを確かめる(合わなければ終了コード1)
// 中身(VRMの拡張は無い、ふつうの glTF):
//   - メッシュ1つ(材質ごとのプリミティブ。POSITION・NORMAL・JOINTS_0・WEIGHTS_0。UV無し)、材質(色だけ)、スキン(ボーンのノードの階層)
//   - アニメーション: rest(休止ポーズ。1キー)、idle・walkFast・walkSlow(1周期。ループ)、attackStand・attackJump・death(全体)。30fps、LINEAR。
//     個体差は無し(標準の動き)。攻撃Bの跳ぶ位置(前へ進む分と高さ)は root の translation、潰れ・伸びは root の scale(地面 Y=0 が基準)
//   - 単位はメートル(enemies.lua の length などで合わせた、ゲームの中の大きさ)。座標は glTF の右手系(+Y上、正面+Z)
// Blender での使い方(メモ):
//   - 読み込み: ファイル > インポート > glTF 2.0。既定の設定でよい(「ボーンの方向」は Blender か Temperance。ボーンの向きは見た目だけで、動きは変わらない)。
//     Blender は Z が上なので、+Y上 → +Z上 に直して読まれる(正面は -Y。フロントビュー(テンキー1)で顔がこちらを向く)
//   - アニメーションは、glTF のアニメーションごとに「アクション」として入る(名前は rest / idle など)。最初の1つがアーマチュアに付き、
//     残りは NLA のトラックに入る。ドープシート > アクションエディターで切り替えて見る・直す
//   - root ボーンの位置・拡大縮小にも、キーがある(跳ぶ移動・潰れ)。全部のアクションが、同じボーン・同じ種類のチャンネルを持つ(切り替えても前の姿勢が残らない)
//   - 書き出し直し: ファイル > エクスポート > glTF 2.0、形式は glTF バイナリ(.glb)、「+Y上」をオン、アニメーションの「モード」を「アクション」にして、
//     全部のアクションを書き出す。ゲームはいまは、この .glb のアニメーションを読まない(形だけ。動きは CreatureAnimator の数式)
#include "model/CreatureBuilder.h"
#include "model/GlbWriter.h"
#include "model/GltfLoader.h"
#include "model/Skeleton.h"
#include "resources/ResourcePaths.h"
#include "scene/game/CreatureAnimator.h"
#include "scene/game/EnemyHorde.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace
{

using Motion = game::CreatureAnimator::Motion;
constexpr float kFps = 30.0f;
constexpr float kEpsilon = 1e-5f; // これより変化しないチャンネルは、1つのキーにする

// 座標系の変換(ModelData の左手系 → glTF の右手系): Z方向の鏡像。位置は z を反転する(scale を掛けてメートルにする)。
// 回転(x,y,z,w)は、鏡像で回転軸の x,y の成分の符号が変わる(GltfLoader の Axes と同じ式)
model::Vec3 toGltf(const model::Vec3 &p, float scale) { return {p.x * scale, p.y * scale, -p.z * scale}; }
model::Quat toGltf(const model::Quat &q) { return {-q.x, -q.y, q.z, q.w}; }

// ノード(ボーン)の局所の姿勢(glTF の座標)
struct NodePose
{
	model::Vec3 translation;
	model::Quat rotation;
	model::Vec3 scale{1.0f, 1.0f, 1.0f};
};

// 書き出すアニメーション。length は長さ(秒)、loop なら最後のキーが最初と同じ(1周期)
struct Clip
{
	const char *name;
	Motion motion;
	float length;
	bool loop;
	bool rest; // 休止ポーズ(1キー)
};

// clip の、時刻 t の全部のボーンの姿勢を求める(個体差は無し)。攻撃Bの跳ぶ移動・高さ(EnemyHorde と同じ放物線)と、潰れを root に入れる
std::vector<NodePose> bakePose(const game::CreatureAnimator &animator, model::Skeleton &skeleton, const game::EnemyType &type, float scale,
	float jumpTravel, const Clip &clip, float t)
{
	game::CreatureAnimator::State state;
	state.motion = state.previous = clip.motion;
	state.clock = t;
	state.actionTime = t;
	state.walkPhase = clip.loop && clip.length > 0.0f ? t / clip.length : 0.0f;
	if(clip.rest){
		skeleton.resetPose();
	}
	else{
		animator.apply(skeleton, state);
	}
	std::vector<NodePose> poses(skeleton.boneCount());
	for(size_t i = 0; i < poses.size(); ++i){
		const int bone = static_cast<int>(i);
		const model::Vec3 &rest = skeleton.restLocalTranslation(bone), &moved = skeleton.boneTranslation(bone);
		poses[i].translation = toGltf({rest.x + moved.x, rest.y + moved.y, rest.z + moved.z}, scale);
		poses[i].rotation = toGltf(skeleton.boneRotation(bone));
	}
	if(clip.rest){
		return poses;
	}
	const int root = skeleton.findBone("root");
	if(root >= 0){
		NodePose &r = poses[static_cast<size_t>(root)];
		const float p = animator.jumpProgress(state);
		r.translation.y += type.creatureMotion.jump.height * 4.0f * p * (1.0f - p);
		r.translation.z += jumpTravel * p; // 正面は glTF の +Z
		const game::CreatureAnimator::BodyScale squash = animator.bodyScale(state);
		r.scale = {squash.horizontal, squash.vertical, squash.horizontal};
	}
	return poses;
}

// clip のキーの時刻: ループは1周期を等分(最後は周期の終わり = 最初と同じ姿勢)、それ以外は 1/30 秒ごと(最後は全体の長さ)
std::vector<float> sampleTimes(const Clip &clip)
{
	std::vector<float> times;
	if(clip.rest || clip.length <= 0.0f){
		times.push_back(0.0f);
		return times;
	}
	if(clip.loop){
		const int n = std::max(2, static_cast<int>(std::lround(clip.length * kFps)));
		for(int i = 0; i <= n; ++i){
			times.push_back(clip.length * static_cast<float>(i) / static_cast<float>(n));
		}
		return times;
	}
	const int n = static_cast<int>(std::ceil(clip.length * kFps - 1e-3f));
	for(int i = 0; i <= n; ++i){
		times.push_back(std::min(static_cast<float>(i) / kFps, clip.length));
	}
	return times;
}

// チャンネル(path 0=translation、1=rotation、2=scale)の値を、成分の並びで返す
int channelComponents(int path) { return path == 1 ? 4 : 3; }
void channelValue(const NodePose &pose, int path, float *out)
{
	switch(path){
	case 0: out[0] = pose.translation.x; out[1] = pose.translation.y; out[2] = pose.translation.z; break;
	case 1: out[0] = pose.rotation.x; out[1] = pose.rotation.y; out[2] = pose.rotation.z; out[3] = pose.rotation.w; break;
	default: out[0] = pose.scale.x; out[1] = pose.scale.y; out[2] = pose.scale.z; break;
	}
}
constexpr const char *kPathNames[3] = {"translation", "rotation", "scale"};

std::string vec(const float *v, int n)
{
	std::string out = "[";
	for(int k = 0; k < n; ++k){
		out += (k ? "," : "") + model::GlbWriter::number(v[k]);
	}
	return out + "]";
}

} // namespace

int main(int argc, char *argv[])
{
	if(argc > 3){
		SDL_Log("usage: creature2glb [out.glb] [enemy type name]");
		return 2;
	}
	const std::string outPath = argc > 1 ? argv[1] : "res/model/enemy/creature.glb";
	const std::string typeName = argc > 2 ? argv[2] : std::string();
	const std::vector<game::EnemyType> types = game::loadEnemyTypes();
	const auto found = std::find_if(types.begin(), types.end(), [&](const game::EnemyType &t){
		return t.creature && (typeName.empty() || t.name == typeName);
	});
	if(found == types.end()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "creature2glb: no creature type '%s' in enemies.lua", typeName.c_str());
		return 1;
	}
	const game::EnemyType &type = *found;
	const auto data = model::buildCreature(*type.creature, type.name);
	const float scale = game::measureEnemy(type, data->vertices).scale;
	model::Skeleton skeleton(data->bones);
	const game::CreatureAnimator animator(skeleton, data->vertices, type.creatureMotion, 1.0f / scale);
	const size_t boneCount = data->bones.size();

	// 攻撃Bで前へ跳ぶ距離: 攻撃Bを始められる距離(jumpMin〜jumpRange)の真ん中から、着地点の、プレイヤーまでの距離を引いたもの(distance まで)
	const auto &motion = type.creatureMotion;
	const auto &behavior = type.behavior;
	const float jumpTravel = std::clamp(0.5f * (behavior.jumpMin + behavior.jumpRange) - motion.jump.landGap, 0.0f, std::max(motion.jump.distance, 0.0f));
	const std::vector<Clip> clips = {
		{"rest", Motion::Idle, 0.0f, false, true},
		{"idle", Motion::Idle, 2.0f * std::max(motion.idle.period, 0.1f), true, false}, // 前足の振りが呼吸の半分の速さなので、2周期で1周
		{"walkFast", Motion::WalkFast, std::max(motion.walkFast.stride, 0.05f) / std::max(motion.walkFast.speed, 1e-3f), true, false},
		{"walkSlow", Motion::WalkSlow, std::max(motion.walkSlow.stride, 0.05f) / std::max(motion.walkSlow.speed, 1e-3f), true, false},
		{"attackStand", Motion::AttackStand, animator.duration(Motion::AttackStand), false, false},
		{"attackJump", Motion::AttackJump, animator.duration(Motion::AttackJump), false, false},
		{"death", Motion::Death, std::max(motion.death.duration, 0.05f), false, false},
	};

	// 焼き込み: clip ごとに、キーの時刻ごとの全部のボーンの姿勢
	std::vector<std::vector<float>> clipTimes;
	std::vector<std::vector<std::vector<NodePose>>> clipPoses;
	for(const Clip &clip : clips){
		clipTimes.push_back(sampleTimes(clip));
		auto &frames = clipPoses.emplace_back();
		for(const float t : clipTimes.back()){
			frames.push_back(bakePose(animator, skeleton, type, scale, jumpTravel, clip, t));
		}
	}
	const std::vector<NodePose> &restPose = clipPoses.front().front();
	// どれかの clip で休止ポーズから動くチャンネルだけを、全部の clip に書く(動かない clip では1キー)
	std::vector<std::array<bool, 3>> animated(boneCount, {false, false, false});
	for(const auto &frames : clipPoses){
		for(const auto &pose : frames){
			for(size_t b = 0; b < boneCount; ++b){
				for(int path = 0; path < 3; ++path){
					float v[4], r[4];
					channelValue(pose[b], path, v);
					channelValue(restPose[b], path, r);
					for(int k = 0; k < channelComponents(path); ++k){
						animated[b][static_cast<size_t>(path)] = animated[b][static_cast<size_t>(path)] || std::fabs(v[k] - r[k]) > kEpsilon;
					}
				}
			}
		}
	}

	model::GlbWriter writer;
	// メッシュ: 材質ごとのプリミティブ。頂点は、その材質の三角形が使うものだけを持つ(エンジンの読み込みは、プリミティブごとに頂点を読むので、共有すると数が増える)。
	// Zの鏡像で三角形の向きが逆になるので、頂点の順を (0,2,1) にする
	std::string primitives, materials;
	std::vector<uint32_t> loadOrder; // 書いた頂点の、元の番号(読み直しの確認用。プリミティブの順に並ぶ)
	int primitiveCount = 0;
	for(const auto &material : data->materials){
		if(material.indexCount == 0){
			continue;
		}
		std::vector<int> remap(data->vertices.size(), -1);
		std::vector<uint32_t> used, indices;
		for(uint32_t i = 0; i < material.indexCount; i += 3){
			const uint32_t *tri = &data->indices[material.firstIndex + i];
			for(const uint32_t v : {tri[0], tri[2], tri[1]}){
				if(remap[v] < 0){
					remap[v] = static_cast<int>(used.size());
					used.push_back(v);
				}
				indices.push_back(static_cast<uint32_t>(remap[v]));
			}
		}
		std::vector<float> positions, normals, weights;
		std::vector<uint16_t> joints;
		float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
		for(const uint32_t v : used){
			const model::ModelVertex &vertex = data->vertices[v];
			const model::Vec3 p = toGltf({vertex.position[0], vertex.position[1], vertex.position[2]}, scale);
			const model::Vec3 n = toGltf({vertex.normal[0], vertex.normal[1], vertex.normal[2]}, 1.0f);
			const float pv[3] = {p.x, p.y, p.z};
			for(int k = 0; k < 3; ++k){
				lo[k] = std::min(lo[k], pv[k]);
				hi[k] = std::max(hi[k], pv[k]);
			}
			positions.insert(positions.end(), {p.x, p.y, p.z});
			normals.insert(normals.end(), {n.x, n.y, n.z});
			float total = 0.0f;
			for(int k = 0; k < 4; ++k){
				total += vertex.bones[k] >= 0 ? vertex.weights[k] : 0.0f;
			}
			for(int k = 0; k < 4; ++k){
				const bool valid = vertex.bones[k] >= 0 && total > 0.0f;
				joints.push_back(static_cast<uint16_t>(valid ? vertex.bones[k] : 0));
				weights.push_back(valid ? vertex.weights[k] / total : 0.0f);
			}
		}
		loadOrder.insert(loadOrder.end(), used.begin(), used.end());
		const int position = writer.addAccessor(positions.data(), used.size(), 5126, 3, "VEC3", ",\"min\":" + vec(lo, 3) + ",\"max\":" + vec(hi, 3));
		const int normal = writer.addAccessor(normals, 3, "VEC3", false);
		const int joint = writer.addAccessor(joints.data(), used.size(), 5123, 4, "VEC4");
		const int weight = writer.addAccessor(weights, 4, "VEC4", false);
		const int index = writer.addAccessor(indices.data(), indices.size(), 5125, 1, "SCALAR");
		primitives += (primitiveCount ? "," : "") + std::string("{\"attributes\":{\"POSITION\":") + std::to_string(position) + ",\"NORMAL\":"
			+ std::to_string(normal) + ",\"JOINTS_0\":" + std::to_string(joint) + ",\"WEIGHTS_0\":" + std::to_string(weight) + "},\"indices\":"
			+ std::to_string(index) + ",\"material\":" + std::to_string(primitiveCount) + "}";
		// 材質: 拡散色を基本色に。金属でなく、鏡面の強い材質(顔)ほど、少しつやを出す
		const float roughness = std::clamp(0.9f - 0.3f * material.specular, 0.7f, 0.9f);
		materials += (primitiveCount ? "," : "") + std::string("{\"name\":\"") + material.name + "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":"
			+ vec(material.diffuse, 4) + ",\"metallicFactor\":0,\"roughnessFactor\":" + model::GlbWriter::number(roughness) + "}}";
		++primitiveCount;
	}

	// ノード: ボーン(番号はボーンと同じ。休止ポーズの回転は無いので、平行移動だけ)、最後にメッシュのノード。
	// スキンの逆行列: 休止ポーズのボーンのワールドの位置の、平行移動の逆(列優先)
	std::string nodes, jointList, sceneNodes;
	std::vector<float> inverseBind;
	for(size_t b = 0; b < boneCount; ++b){
		const model::ModelBone &bone = data->bones[b];
		float t[3];
		channelValue(restPose[b], 0, t);
		std::string children;
		for(size_t c = 0; c < boneCount; ++c){
			if(data->bones[c].parent == static_cast<int>(b)){
				children += (children.empty() ? "" : ",") + std::to_string(c);
			}
		}
		nodes += (b ? "," : "") + std::string("{\"name\":\"") + bone.name + "\",\"translation\":" + vec(t, 3)
			+ (children.empty() ? "" : ",\"children\":[" + children + "]") + "}";
		jointList += (b ? "," : "") + std::to_string(b);
		if(bone.parent < 0){
			sceneNodes += (sceneNodes.empty() ? "" : ",") + std::to_string(b);
		}
		const model::Vec3 p = toGltf({bone.position[0], bone.position[1], bone.position[2]}, scale);
		inverseBind.insert(inverseBind.end(), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p.x, -p.y, -p.z, 1});
	}
	nodes += ",{\"name\":\"" + type.name + "\",\"mesh\":0,\"skin\":0}";
	sceneNodes += "," + std::to_string(boneCount);
	const int inverseBindAccessor = writer.addAccessor(inverseBind, 16, "MAT4", false);

	// アニメーション
	std::string animations;
	for(size_t c = 0; c < clips.size(); ++c){
		const auto &times = clipTimes[c];
		const auto &frames = clipPoses[c];
		const int input = writer.addAccessor(times, 1, "SCALAR", true);
		int singleInput = -1; // 動かないチャンネルの、1キーの時刻(0)
		std::string samplers, channels;
		int samplerCount = 0;
		for(size_t b = 0; b < boneCount; ++b){
			for(int path = 0; path < 3; ++path){
				if(!animated[b][static_cast<size_t>(path)]){
					continue;
				}
				const int n = channelComponents(path);
				std::vector<float> values;
				bool varies = false;
				for(size_t f = 0; f < frames.size(); ++f){
					float v[4];
					channelValue(frames[f][b], path, v);
					// 回転は、前のキーと同じ半球にそろえる(補間が遠回りしない)
					if(path == 1 && f > 0){
						const float *prev = &values[values.size() - 4];
						if(prev[0] * v[0] + prev[1] * v[1] + prev[2] * v[2] + prev[3] * v[3] < 0.0f){
							for(float &x : v){ x = -x; }
						}
					}
					for(int k = 0; k < n; ++k){
						varies = varies || (f > 0 && std::fabs(v[k] - values[static_cast<size_t>(k)]) > kEpsilon);
						values.push_back(v[k]);
					}
				}
				if(!varies){
					values.resize(static_cast<size_t>(n));
					if(singleInput < 0){
						singleInput = writer.addAccessor(std::vector<float>{0.0f}, 1, "SCALAR", true);
					}
				}
				const int output = writer.addAccessor(values, n, n == 4 ? "VEC4" : "VEC3", false);
				samplers += (samplerCount ? "," : "") + std::string("{\"input\":") + std::to_string(varies ? input : singleInput)
					+ ",\"interpolation\":\"LINEAR\",\"output\":" + std::to_string(output) + "}";
				channels += (samplerCount ? "," : "") + std::string("{\"sampler\":") + std::to_string(samplerCount) + ",\"target\":{\"node\":"
					+ std::to_string(b) + ",\"path\":\"" + kPathNames[path] + "\"}}";
				++samplerCount;
			}
		}
		animations += (c ? "," : "") + std::string("{\"name\":\"") + clips[c].name + "\",\"samplers\":[" + samplers + "],\"channels\":[" + channels + "]}";
		SDL_Log("creature2glb: animation '%s' %.3f s, %zu keys, %d channels", clips[c].name, static_cast<double>(times.back()), times.size(), samplerCount);
	}

	const std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"SDL3_Lua5_Vulkan creature2glb\"},"
		"\"scene\":0,\"scenes\":[{\"name\":\"" + type.name + "\",\"nodes\":[" + sceneNodes + "]}],\"nodes\":[" + nodes + "],"
		"\"meshes\":[{\"name\":\"" + type.name + "\",\"primitives\":[" + primitives + "]}],\"materials\":[" + materials + "],"
		"\"skins\":[{\"name\":\"" + type.name + "\",\"inverseBindMatrices\":" + std::to_string(inverseBindAccessor) + ",\"skeleton\":0,\"joints\":[" + jointList + "]}],"
		"\"animations\":[" + animations + "],"
		"\"accessors\":[" + writer.accessors() + "],\"bufferViews\":[" + writer.views() + "],"
		"\"buffers\":[{\"byteLength\":" + std::to_string(writer.bin().size()) + "}]}";
	const std::string fullPath = ResourcePaths::resource(outPath.c_str());
	if(!writer.save(fullPath, json)){
		return 1;
	}
	SDL_Log("creature2glb: wrote %s ('%s', scale %.4f, %zu vertices, %zu triangles, %zu bones, %d primitives)", fullPath.c_str(), type.name.c_str(),
		static_cast<double>(scale), loadOrder.size(), data->indices.size() / 3, boneCount, primitiveCount);

	// 確認: エンジンの読み込みで読み直す(ModelData の座標系へ戻るので、元の頂点・ボーンに scale を掛けたものと一致するはず)。
	// 読み込みは全部のノードをボーンにするので、ボーンはメッシュのノードの分だけ1つ多い
	const auto loaded = model::loadVrm(fullPath);
	if(!loaded){
		return 1;
	}
	bool ok = loaded->vertices.size() == data->vertices.size() && loaded->vertices.size() == loadOrder.size()
		&& loaded->indices.size() == data->indices.size() && loaded->bones.size() == boneCount + 1;
	float vertexError = 0.0f, boneError = 0.0f;
	for(size_t i = 0; ok && i < loadOrder.size(); ++i){
		const model::ModelVertex &a = loaded->vertices[i], &b = data->vertices[loadOrder[i]];
		for(int k = 0; k < 3; ++k){
			vertexError = std::max(vertexError, std::fabs(a.position[k] - b.position[k] * scale));
		}
		ok = a.bones[0] == b.bones[0];
	}
	for(size_t i = 0; ok && i < boneCount; ++i){
		const model::ModelBone &a = loaded->bones[i], &b = data->bones[i];
		for(int k = 0; k < 3; ++k){
			boneError = std::max(boneError, std::fabs(a.position[k] - b.position[k] * scale));
		}
		ok = a.name == b.name && a.parent == b.parent;
	}
	ok = ok && vertexError < 1e-4f && boneError < 1e-4f;
	SDL_Log("creature2glb: reload %s: %zu vertices, %zu triangles, %zu bones (+1 mesh node), max error vertex %.2g m, bone %.2g m",
		ok ? "ok" : "MISMATCH", loaded->vertices.size(), loaded->indices.size() / 3, loaded->bones.size() - 1, static_cast<double>(vertexError),
		static_cast<double>(boneError));
	return ok ? 0 : 1;
}
