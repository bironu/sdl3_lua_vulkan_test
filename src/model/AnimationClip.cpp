#include "model/AnimationClip.h"
#include "model/GltfFile.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace model
{

namespace
{

// 時刻 t を挟むキー(i と i+1)と、その間の割合。最初のキーより前・最後のキーより後は、端のキー(割合0)
template<typename T>
size_t findKey(const AnimationClip::Curve<T> &curve, float t, float &fraction)
{
	fraction = 0.0f;
	const auto &times = curve.times;
	if(times.size() < 2 || t <= times.front()){
		return 0;
	}
	if(t >= times.back()){
		return times.size() - 1;
	}
	const size_t i = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1;
	const float span = times[i + 1] - times[i];
	if(curve.interpolation == AnimationClip::Interpolation::Linear && span > 0.0f){
		fraction = (t - times[i]) / span;
	}
	return i;
}

// 回転行列(q の)の成分の2乗で、局所の軸の倍率 r を、モデルの軸の倍率へ移す(軸が90度ずつ入れ替わる回転なら正確。拡大縮小は、軸に沿ったものだけを扱う)
Vec3 scaleToModelAxes(const Quat &q, const Vec3 &r)
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
	const float m[3][3] = {
		{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
		{2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
		{2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)},
	};
	const float local[3] = {r.x, r.y, r.z};
	float out[3] = {0.0f, 0.0f, 0.0f};
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			out[i] += m[i][j] * m[i][j] * local[j];
		}
	}
	return {out[0], out[1], out[2]};
}

} // namespace

template<>
Vec3 AnimationClip::Curve<Vec3>::sample(float t) const
{
	if(values.empty()){
		return {};
	}
	float f;
	const size_t i = findKey(*this, t, f);
	const Vec3 &a = values[i];
	if(f <= 0.0f){
		return a;
	}
	const Vec3 &b = values[i + 1];
	return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
}

template<>
Quat AnimationClip::Curve<Quat>::sample(float t) const
{
	if(values.empty()){
		return {};
	}
	float f;
	const size_t i = findKey(*this, t, f);
	return f <= 0.0f ? values[i] : Quat::slerp(values[i], values[i + 1], f);
}

std::vector<AnimationClip> loadAnimationClips(const std::string &fullPath)
{
	std::vector<AnimationClip> clips;
	try{
		const GltfFile gltf(fullPath);
		const JsonValue &json = gltf.json();
		const GltfAxes axes = GltfAxes::of(json);
		const JsonValue &nodes = json["nodes"];
		const size_t nodeCount = nodes.size();

		// ノードの休止ポーズ(エンジンの座標): 局所の移動・回転・拡大縮小と、グローバルな回転(親から順に掛ける)
		struct Rest
		{
			Vec3 translation;
			Quat rotation, global;
			Vec3 scale{1.0f, 1.0f, 1.0f};
			int parent = -1;
			std::string name;
		};
		std::vector<Rest> rest(nodeCount);
		const std::vector<int> parents = gltf.nodeParents();
		for(const int n : gltf.nodeOrder(parents)){
			GltfFile::NodeTransform trs = gltf.nodeTransform(static_cast<size_t>(n));
			axes.point(trs.translation);
			axes.quaternion(trs.rotation);
			Rest &r = rest[static_cast<size_t>(n)];
			r.parent = parents[static_cast<size_t>(n)];
			r.name = nodes[static_cast<size_t>(n)]["name"].asString("node" + std::to_string(n));
			r.translation = {static_cast<float>(trs.translation[0]), static_cast<float>(trs.translation[1]), static_cast<float>(trs.translation[2])};
			r.rotation = Quat{static_cast<float>(trs.rotation[0]), static_cast<float>(trs.rotation[1]), static_cast<float>(trs.rotation[2]),
				static_cast<float>(trs.rotation[3])}.normalized();
			r.scale = {static_cast<float>(trs.scale[0]), static_cast<float>(trs.scale[1]), static_cast<float>(trs.scale[2])};
			r.global = r.parent >= 0 ? (rest[static_cast<size_t>(r.parent)].global * r.rotation).normalized() : r.rotation;
		}

		const JsonValue &animations = json["animations"];
		bool warnedCubic = false;
		for(size_t a = 0; a < animations.size(); ++a){
			const JsonValue &animation = animations[a];
			AnimationClip clip;
			clip.name = animation["name"].asString("animation" + std::to_string(a));
			const JsonValue &samplers = animation["samplers"];
			const JsonValue &channels = animation["channels"];
			for(size_t c = 0; c < channels.size(); ++c){
				const JsonValue &target = channels[c]["target"];
				const int node = target["node"].asInt(-1);
				const std::string path = target["path"].asString();
				const JsonValue &sampler = samplers[static_cast<size_t>(channels[c]["sampler"].asInt(-1))];
				const int components = path == "rotation" ? 4 : (path == "translation" || path == "scale" ? 3 : 0);
				if(node < 0 || static_cast<size_t>(node) >= nodeCount || sampler.isNull() || components == 0){
					continue; // モーフの重み(weights)などは扱わない
				}
				const std::string interpolation = sampler["interpolation"].asString("LINEAR");
				const bool cubic = interpolation == "CUBICSPLINE";
				if(cubic && !warnedCubic){
					SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "animation clip: CUBICSPLINE is read as LINEAR (key values only) (%s)", fullPath.c_str());
					warnedCubic = true;
				}
				const GltfFile::Accessor times = gltf.readAccessor(sampler["input"].asInt(-1));
				const GltfFile::Accessor values = gltf.readAccessor(sampler["output"].asInt(-1));
				// CUBICSPLINE は、キーごとに (入りの接線, 値, 出の接線) の3つ。値だけを使う
				const size_t stride = cubic ? 3 : 1, offset = cubic ? 1 : 0;
				if(times.count == 0 || times.components != 1 || values.components != components || values.count != times.count * stride){
					SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "animation clip '%s': channel %zu has bad keys (ignored) (%s)", clip.name.c_str(), c, fullPath.c_str());
					continue;
				}
				const Rest &r = rest[static_cast<size_t>(node)];
				const Quat parentGlobal = r.parent >= 0 ? rest[static_cast<size_t>(r.parent)].global : Quat{};
				auto track = std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const AnimationClip::Track &t){ return t.node == r.name; });
				if(track == clip.tracks.end()){
					clip.tracks.emplace_back();
					track = clip.tracks.end() - 1;
					track->node = r.name;
				}
				const AnimationClip::Interpolation mode = interpolation == "STEP" ? AnimationClip::Interpolation::Step : AnimationClip::Interpolation::Linear;
				auto value = [&](size_t key, int k){ return values.values[(key * stride + offset) * static_cast<size_t>(components) + static_cast<size_t>(k)]; };
				auto fill = [&](auto &curve, auto &&convert){
					curve.times = times.values;
					curve.interpolation = mode;
					curve.values.clear();
					curve.values.reserve(times.count);
					for(size_t key = 0; key < times.count; ++key){
						curve.values.push_back(convert(key));
					}
				};
				if(path == "translation"){
					// 休止ポーズからのずれを、親の休止ポーズのグローバルな向きで回して、モデルの軸にする
					fill(track->translation, [&](size_t key){
						double p[3] = {value(key, 0), value(key, 1), value(key, 2)};
						axes.point(p);
						return parentGlobal.rotate({static_cast<float>(p[0]) - r.translation.x, static_cast<float>(p[1]) - r.translation.y,
							static_cast<float>(p[2]) - r.translation.z});
					});
				}
				else if(path == "rotation"){
					// 局所の回転 q の、休止ポーズ q0 からの差 q q0^-1 を、親の休止ポーズのグローバルな向き P で、モデルの軸にする: P (q q0^-1) P^-1。
					// 前のキーと同じ半球にそろえる(補間が遠回りしない)
					Quat previous;
					fill(track->rotation, [&](size_t key){
						double q[4] = {value(key, 0), value(key, 1), value(key, 2), value(key, 3)};
						axes.quaternion(q);
						const Quat local = Quat{static_cast<float>(q[0]), static_cast<float>(q[1]), static_cast<float>(q[2]), static_cast<float>(q[3])}.normalized();
						Quat delta = (parentGlobal * local * r.rotation.conjugate() * parentGlobal.conjugate()).normalized();
						if(key > 0 && delta.x * previous.x + delta.y * previous.y + delta.z * previous.z + delta.w * previous.w < 0.0f){
							delta = {-delta.x, -delta.y, -delta.z, -delta.w};
						}
						previous = delta;
						return delta;
					});
				}
				else{
					// 休止ポーズの拡大縮小に対する倍率を、ノードの休止ポーズのグローバルな向きで、モデルの軸へ移す
					auto ratio = [](float v, float base){ return std::fabs(base) > 1e-6f ? v / base : 1.0f; };
					fill(track->scale, [&](size_t key){
						return scaleToModelAxes(r.global, {ratio(value(key, 0), r.scale.x), ratio(value(key, 1), r.scale.y), ratio(value(key, 2), r.scale.z)});
					});
				}
				clip.duration = std::max(clip.duration, times.values.back());
			}
			clips.push_back(std::move(clip));
		}
		if(clips.empty()){
			throw std::runtime_error("no animations");
		}
	}
	catch(const std::exception &e){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "animation clip load error: %s (%s)", e.what(), fullPath.c_str());
		clips.clear();
	}
	return clips;
}

} // namespace model
