#include "creature/CreatureBuilder.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace model
{

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

struct V3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 operator+(const V3 &a, const V3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3 &a, const V3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(const V3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3 &a, const V3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(const V3 &a) { return std::sqrt(dot(a, a)); }
V3 normalize(const V3 &a, const V3 &fallback = {0.0f, 1.0f, 0.0f})
{
	const float l = length(a);
	return l > 1e-8f ? a * (1.0f / l) : fallback;
}
V3 toV3(const float (&a)[3]) { return {a[0], a[1], a[2]}; }

float smoothstep(float edge0, float edge1, float x)
{
	if(edge1 <= edge0){
		return x < edge0 ? 0.0f : 1.0f;
	}
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// 体の向きの言葉(外・上・前)で書いた点を、モデルの座標(左手系。正面が-Z)へ。side は左 +1、右 -1(左の手足が+X)
V3 toModel(const V3 &outUpForward, float side) { return {outUpForward.x * side, outUpForward.y, -outUpForward.z}; }

enum Material { Back, Belly, Limb, Face, MaterialCount };

// 作りかけのメッシュ: 頂点と、材質ごとの三角形の並び
class MeshBuilder
{
public:
	MeshBuilder(std::vector<ModelVertex> &vertices, float bellyLine) : vertices_(vertices), bellyLine_(bellyLine) {}

	// boneA の重み 1-wB、boneB の重み wB(boneB<0 なら boneA だけ)
	uint32_t addVertex(const V3 &p, const V3 &n, int boneA, int boneB = -1, float wB = 0.0f)
	{
		ModelVertex v;
		v.position[0] = p.x;
		v.position[1] = p.y;
		v.position[2] = p.z;
		v.normal[0] = n.x;
		v.normal[1] = n.y;
		v.normal[2] = n.z;
		v.uv[0] = v.uv[1] = 0.0f;
		if(boneB < 0 || wB <= 0.0f){
			v.bones[0] = boneA;
			v.weights[0] = 1.0f;
		}
		else if(wB >= 1.0f){
			v.bones[0] = boneB;
			v.weights[0] = 1.0f;
		}
		else{
			v.bones[0] = boneA;
			v.weights[0] = 1.0f - wB;
			v.bones[1] = boneB;
			v.weights[1] = wB;
		}
		vertices_.push_back(v);
		return static_cast<uint32_t>(vertices_.size() - 1);
	}

	// 三角形を足す。面の向き(辺の外積)が、頂点の法線と同じ側になるように、頂点の順を直す(元の座標系で、外積が外を向く順。
	// VulkanModel がZ反転と頂点の入れ替えをして、表面にそろえる)。潰れた三角形は捨てる。
	// material が Back なら、面の法線の上下で、背(Back)と腹(Belly)に分ける
	void addTriangle(uint32_t a, uint32_t b, uint32_t c, Material material)
	{
		const V3 pa = position(a), pb = position(b), pc = position(c);
		const V3 face = cross(pb - pa, pc - pa);
		if(length(face) < 1e-12f){
			return;
		}
		const V3 n = normal(a) + normal(b) + normal(c);
		if(dot(face, n) < 0.0f){
			std::swap(b, c);
		}
		if(material == Back && normalize(n).y < bellyLine_){
			material = Belly;
		}
		auto &list = indices_[material];
		list.insert(list.end(), {a, b, c});
	}

	void addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, Material material)
	{
		addTriangle(a, b, c, material);
		addTriangle(a, c, d, material);
	}

	std::array<std::vector<uint32_t>, MaterialCount> &indices() { return indices_; }

private:
	V3 position(uint32_t i) const { return toV3(vertices_[i].position); }
	V3 normal(uint32_t i) const { return toV3(vertices_[i].normal); }

	std::vector<ModelVertex> &vertices_;
	float bellyLine_;
	std::array<std::vector<uint32_t>, MaterialCount> indices_;
};

// 卵形の、前後方向の断面の大きさ: 前の端(phi=0)から後ろの端(phi=π)へ、sin(phi)(1 - taper cos(phi))
float eggProfile(float phi, float taper) { return std::sin(phi) * (1.0f - taper * std::cos(phi)); }

// eggProfile のいちばん大きい値(直径の指定を、いちばん太い所に合わせる)
float eggProfileMax(float taper)
{
	float best = 0.0f;
	for(int i = 0; i <= 200; ++i){
		best = std::max(best, eggProfile(kPi * static_cast<float>(i) / 200.0f, taper));
	}
	return best;
}

// 卵形(taper 0 で楕円体)。半径は 外・上・前 の方向(rf は前後の半分の長さ)。法線は面の式から解析的に求める
void addEgg(MeshBuilder &mesh, const V3 &center, float rx, float ry, float rf, float taper, int slices, int sides, int bone, Material material)
{
	slices = std::max(slices, 3);
	sides = std::max(sides, 3);
	const float scale = 1.0f / eggProfileMax(taper); // 断面のいちばん大きい所を、半径 rx・ry に合わせる
	// 点: (rx g cos t, ry g sin t, rf cos phi)、g = eggProfile(phi) * scale。
	// 法線は、偏微分の外積を g で割った (rf ry sin(phi) cos t, rf rx sin(phi) sin t, rx ry dg/dphi)(両端でも潰れない)
	const auto vertex = [&](float phi, float t){
		const float s = std::cos(phi), sp = std::sin(phi);
		const float g = eggProfile(phi, taper) * scale;
		const float dg = (s * (1.0f - taper * s) + taper * sp * sp) * scale;
		const V3 p{rx * g * std::cos(t), ry * g * std::sin(t), rf * s};
		const V3 n{rf * ry * sp * std::cos(t), rf * rx * sp * std::sin(t), rx * ry * dg};
		return mesh.addVertex(center + toModel(p, 1.0f), normalize(toModel(n, 1.0f)), bone);
	};
	const uint32_t front = vertex(0.0f, 0.0f);
	std::vector<uint32_t> previous(static_cast<size_t>(sides), front);
	for(int i = 1; i <= slices; ++i){
		const float phi = kPi * static_cast<float>(i) / static_cast<float>(slices);
		std::vector<uint32_t> ring(static_cast<size_t>(sides));
		if(i == slices){
			std::fill(ring.begin(), ring.end(), vertex(kPi, 0.0f));
		}
		else{
			for(int k = 0; k < sides; ++k){
				ring[static_cast<size_t>(k)] = vertex(phi, 2.0f * kPi * static_cast<float>(k) / static_cast<float>(sides));
			}
		}
		for(size_t k = 0; k < ring.size(); ++k){
			const size_t k1 = (k + 1) % ring.size();
			mesh.addQuad(previous[k], previous[k1], ring[k1], ring[k], material);
		}
		previous = std::move(ring);
	}
}

// 筒の中心線の点。bone は、この点から次の点までの区間を動かすボーン
struct TubePoint
{
	V3 position;
	float radius = 0.01f;
	int bone = -1;
};

// 筒の作り: 関節の丸め・重みの混ぜの幅(区間の長さに対する割合)、区間ごとのリングの数、周の分割数。
// rootBone>=0 なら、最初の点から後ろへ inset(長さ)だけ筒を延ばして(胴体へ入り込ませて)、そこから最初の区間のボーンへ重みを混ぜる
struct TubeStyle
{
	float blend = 0.3f;
	int rings = 6;
	int sides = 8;
	int rootBone = -1;
	float inset = 0.0f;
};

// 関節を丸めた筒(先細りのリングを積み重ね、先は丸く閉じる。付け根を胴体へ入れない筒は、始まりも閉じる)。関節の前後では、2つのボーンの重みを滑らかに混ぜる
void addTube(MeshBuilder &mesh, const std::vector<TubePoint> &points, const TubeStyle &style, Material material)
{
	const size_t n = points.size();
	if(n < 2){
		return;
	}
	std::vector<float> lengths(n - 1), starts(n); // 区間の長さ、点までの中心線の長さ(角を丸める前)
	std::vector<V3> dirs(n - 1);
	for(size_t k = 0; k + 1 < n; ++k){
		const V3 d = points[k + 1].position - points[k].position;
		lengths[k] = std::max(length(d), 1e-5f);
		dirs[k] = d * (1.0f / lengths[k]);
		starts[k + 1] = starts[k] + lengths[k];
	}
	// 関節(内側の点)の、丸める半分の幅
	const float blend = std::clamp(style.blend, 0.0f, 0.45f);
	std::vector<float> corner(n, 0.0f);
	for(size_t j = 1; j + 1 < n; ++j){
		corner[j] = blend * std::min(lengths[j - 1], lengths[j]);
	}
	const bool rooted = style.rootBone >= 0 && style.inset > 0.0f;
	const float rootEnd = blend * lengths[0]; // 付け根の重みを混ぜ終わる所

	const auto segmentOf = [&](float s){
		size_t k = 0;
		while(k + 2 < n && s > starts[k + 1]){
			++k;
		}
		return k;
	};
	const auto radiusAt = [&](float s){
		const size_t k = segmentOf(s);
		const float t = std::clamp((s - starts[k]) / lengths[k], 0.0f, 1.0f);
		return points[k].radius + (points[k + 1].radius - points[k].radius) * t;
	};
	// 中心線の位置と接線(関節のまわりは、2次ベジェ曲線で丸める)
	const auto centerAt = [&](float s, V3 &position, V3 &tangent){
		for(size_t j = 1; j + 1 < n; ++j){
			if(corner[j] > 0.0f && std::fabs(s - starts[j]) < corner[j]){
				const float u = (s - (starts[j] - corner[j])) / (2.0f * corner[j]);
				const V3 &joint = points[j].position;
				const V3 p0 = joint - dirs[j - 1] * corner[j], p2 = joint + dirs[j] * corner[j];
				position = p0 * ((1.0f - u) * (1.0f - u)) + joint * (2.0f * u * (1.0f - u)) + p2 * (u * u);
				tangent = normalize(dirs[j - 1] * (1.0f - u) + dirs[j] * u, dirs[j]);
				return;
			}
		}
		const size_t k = s < 0.0f ? 0 : segmentOf(s);
		position = points[k].position + dirs[k] * (s - starts[k]);
		tangent = dirs[k];
	};
	// 重み: 付け根(胴体のボーン→最初の区間のボーン)、関節(前の区間のボーン→次の区間のボーン)、それ以外は区間のボーン
	const auto weightAt = [&](float s, int &boneA, int &boneB, float &wB){
		if(rooted && s < rootEnd){
			boneA = style.rootBone;
			boneB = points[0].bone;
			wB = smoothstep(-style.inset, rootEnd, s);
			return;
		}
		for(size_t j = 1; j + 1 < n; ++j){
			const float width = std::max(corner[j], 1e-4f);
			if(std::fabs(s - starts[j]) < width){
				boneA = points[j - 1].bone;
				boneB = points[j].bone;
				wB = smoothstep(starts[j] - width, starts[j] + width, s);
				return;
			}
		}
		boneA = points[segmentOf(s)].bone;
		boneB = -1;
		wB = 0.0f;
	};

	// リングを置く位置: 区間ごとに、両端(関節)の近くほど細かく(0.5-0.5cos)
	std::vector<float> stations;
	if(rooted){
		stations.push_back(-style.inset);
		stations.push_back(-0.5f * style.inset);
	}
	const int rings = std::max(style.rings, 1);
	for(size_t k = 0; k + 1 < n; ++k){
		for(int i = k == 0 ? 0 : 1; i <= rings; ++i){
			stations.push_back(starts[k] + lengths[k] * (0.5f - 0.5f * std::cos(kPi * static_cast<float>(i) / static_cast<float>(rings))));
		}
	}

	// 端を丸く閉じる: リングの中心から outward へ radius だけ出した点と、リングを結ぶ
	const auto closeEnd = [&](const std::vector<uint32_t> &ring, const V3 &center, const V3 &outward, float radius, int boneA, int boneB, float wB){
		const uint32_t tip = mesh.addVertex(center + outward * radius, outward, boneA, boneB, wB);
		for(size_t k = 0; k < ring.size(); ++k){
			mesh.addTriangle(ring[k], ring[(k + 1) % ring.size()], tip, material);
		}
	};
	const int sides = std::max(style.sides, 3);
	V3 frameN; // リングの基準の向き(接線に垂直。前のリングから平行移動して、ねじれを防ぐ)
	std::vector<uint32_t> previous;
	V3 lastPosition, lastTangent;
	int lastA = -1, lastB = -1;
	float lastW = 0.0f, lastRadius = 0.0f;
	for(size_t i = 0; i < stations.size(); ++i){
		const float s = stations[i];
		V3 position, tangent;
		centerAt(s, position, tangent);
		const float radius = radiusAt(s);
		const float h = 1e-3f * lengths[0];
		const float slope = (radiusAt(s + h) - radiusAt(s - h)) / (2.0f * h); // 太さの変わり方(法線を傾ける)
		if(i == 0){
			const V3 reference = std::fabs(tangent.y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f};
			frameN = normalize(cross(cross(tangent, reference), tangent));
		}
		else{
			frameN = normalize(frameN - tangent * dot(frameN, tangent), frameN);
		}
		const V3 frameB = cross(tangent, frameN);
		int boneA, boneB;
		float wB;
		weightAt(s, boneA, boneB, wB);
		std::vector<uint32_t> ring(static_cast<size_t>(sides));
		for(int k = 0; k < sides; ++k){
			const float a = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(sides);
			const V3 radial = frameN * std::cos(a) + frameB * std::sin(a);
			ring[static_cast<size_t>(k)] = mesh.addVertex(position + radial * radius, normalize(radial - tangent * slope), boneA, boneB, wB);
		}
		if(!previous.empty()){
			for(size_t k = 0; k < ring.size(); ++k){
				const size_t k1 = (k + 1) % ring.size();
				mesh.addQuad(previous[k], previous[k1], ring[k1], ring[k], material);
			}
		}
		else if(!rooted){
			closeEnd(ring, position, tangent * -1.0f, radius, boneA, boneB, wB);
		}
		previous = std::move(ring);
		lastPosition = position;
		lastTangent = tangent;
		lastA = boneA;
		lastB = boneB;
		lastW = wB;
		lastRadius = radius;
	}
	closeEnd(previous, lastPosition, lastTangent, lastRadius, lastA, lastB, lastW);
}

int addBone(ModelData &data, const std::string &name, const V3 &position, int parent)
{
	ModelBone bone;
	bone.name = name;
	bone.position[0] = position.x;
	bone.position[1] = position.y;
	bone.position[2] = position.z;
	bone.parent = parent;
	bone.flags = ModelBone::Rotatable | ModelBone::Translatable | ModelBone::Visible | ModelBone::Operable;
	data.bones.push_back(std::move(bone));
	return static_cast<int>(data.bones.size() - 1);
}


// 卵形の胴体の表面(addEgg と同じ形)。点は、胴体の中心から見た 外・上・前
class EggSurface
{
public:
	EggSurface(float rx, float ry, float rf, float taper) : rx_(rx), ry_(ry), rf_(rf), taper_(taper), scale_(1.0f / eggProfileMax(taper)) {}

	// 中心から dir の向きへ進んで、表面に着く点(中心から外へ、内側→外側が1回だけ変わるので、二分法で求める)
	V3 along(const V3 &dir) const
	{
		const V3 d = normalize(dir);
		float inside = 0.0f, outside = 2.0f * std::max({rx_, ry_, rf_});
		for(int i = 0; i < 32; ++i){
			const float middle = 0.5f * (inside + outside);
			(level(d * middle) < 0.0f ? inside : outside) = middle;
		}
		return d * (0.5f * (inside + outside));
	}
	// 表面の点 p の、外向きの法線(式の勾配を差分で)
	V3 normal(const V3 &p) const
	{
		const float h = 1e-3f * std::min({rx_, ry_, rf_});
		return normalize({level(p + V3{h, 0.0f, 0.0f}) - level(p - V3{h, 0.0f, 0.0f}),
			level(p + V3{0.0f, h, 0.0f}) - level(p - V3{0.0f, h, 0.0f}),
			level(p + V3{0.0f, 0.0f, h}) - level(p - V3{0.0f, 0.0f, h})});
	}

private:
	// 表面の式: 内側で負、外側で正
	float level(const V3 &p) const
	{
		const float f = p.z / rf_;
		if(std::fabs(f) >= 1.0f){
			return std::fabs(f);
		}
		const float g = eggProfile(std::acos(f), taper_) * scale_;
		const float x = p.x / rx_, y = p.y / ry_;
		return x * x + y * y - g * g;
	}

	float rx_, ry_, rf_, taper_, scale_;
};

// 胴体の前面に、表面に沿って顔の部品(閉じ目の弧2つ・口の楕円の板)を貼る
void addFace(MeshBuilder &mesh, const EggSurface &egg, const V3 &bodyCenter, const CreatureSpec::Face &face, int bone)
{
	// 顔の中心と、表面に沿った向き(外・上)
	const V3 center = egg.along({0.0f, std::sin(face.pitch), std::cos(face.pitch)});
	const V3 n = egg.normal(center);
	const V3 out = normalize(V3{1.0f, 0.0f, 0.0f} - n * n.x);
	const V3 up = cross(n, out); // 外・上・前の座標系(左手系の向きの言葉)で、n=前、out=外 のとき、上
	// 顔の上の (u, v)(外・上のずれ)を、表面へ写し、lift だけ浮かせた点(モデルの座標)と、その法線
	const auto place = [&](float u, float v, float lift, V3 &normal){
		const V3 p = egg.along(center + out * u + up * v);
		const V3 surfaceNormal = egg.normal(p);
		normal = toModel(surfaceNormal, 1.0f);
		return bodyCenter + toModel(p + surfaceNormal * lift, 1.0f);
	};
	// 閉じ目: 上に凸の弧の細い筒(両端も丸く閉じる)
	TubeStyle eyeStyle;
	eyeStyle.blend = 0.0f;
	eyeStyle.rings = 1;
	eyeStyle.sides = 6;
	const int segments = std::max(face.eyeSegments, 2);
	for(const float side : {1.0f, -1.0f}){
		std::vector<TubePoint> points;
		for(int i = 0; i <= segments; ++i){
			const float a = 0.5f * kPi + face.eyeSpan * (static_cast<float>(i) / static_cast<float>(segments) - 0.5f);
			V3 normal;
			points.push_back({place(side * face.eyeSpacing + face.eyeRadius * std::cos(a), face.eyeUp + face.eyeRadius * std::sin(a), face.lift, normal),
				face.eyeThickness, bone});
		}
		addTube(mesh, points, eyeStyle, Face);
	}
	// 口: 縦長の楕円の板(中心から輪を広げる扇)
	constexpr int kMouthRings = 2;
	const int sides = std::max(face.mouthSides, 6);
	V3 normal;
	const V3 mouthCenter = place(0.0f, face.mouthUp, face.lift, normal);
	std::vector<uint32_t> previous(static_cast<size_t>(sides), mesh.addVertex(mouthCenter, normal, bone));
	for(int r = 1; r <= kMouthRings; ++r){
		const float scale = static_cast<float>(r) / kMouthRings;
		std::vector<uint32_t> ring(static_cast<size_t>(sides));
		for(int k = 0; k < sides; ++k){
			const float a = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(sides);
			const V3 p = place(0.5f * face.mouthWidth * scale * std::cos(a), face.mouthUp + 0.5f * face.mouthHeight * scale * std::sin(a), face.lift, normal);
			ring[static_cast<size_t>(k)] = mesh.addVertex(p, normal, bone);
		}
		for(size_t k = 0; k < ring.size(); ++k){
			const size_t k1 = (k + 1) % ring.size();
			mesh.addQuad(previous[k], previous[k1], ring[k1], ring[k], Face);
		}
		previous = std::move(ring);
	}
}
}

std::shared_ptr<ModelData> buildCreature(const CreatureSpec &spec, const std::string &name)
{
	auto data = std::make_shared<ModelData>();
	data->name = name;
	MeshBuilder mesh(data->vertices, spec.bellyLine);
	const auto &body = spec.body;
	const float taper = std::clamp(body.taper, 0.0f, 0.9f);
	const float halfLength = 0.5f * body.length, halfWidth = 0.5f * body.width, halfHeight = 0.5f * body.height;
	const float profileScale = 1.0f / eggProfileMax(taper);

	// 手足の関節の位置(胴体の中心から見た 外・上・前)。足先の端(筒の先の丸み)のいちばん低い所が地面(Y=0)になるよう、胴体の高さを決める
	struct LegJoints
	{
		V3 joint[4]; // 付け根・ひざ・足首・足先
		V3 tip;      // 足先の端(足首→足先の向きへ、足先の太さだけ先)
	};
	std::vector<LegJoints> legJoints;
	legJoints.reserve(spec.legs.size());
	for(const auto &leg : spec.legs){
		const float along = std::clamp(leg.along, -0.98f, 0.98f);
		const float g = eggProfile(std::acos(along), taper) * profileScale;
		LegJoints joints;
		joints.joint[0] = {halfWidth * g * std::cos(leg.angle), halfHeight * g * std::sin(leg.angle), halfLength * along};
		joints.joint[1] = joints.joint[0] + toV3(leg.knee);
		joints.joint[2] = joints.joint[1] + toV3(leg.ankle);
		joints.joint[3] = joints.joint[2] + toV3(leg.foot);
		joints.tip = joints.joint[3] + normalize(toV3(leg.foot), {0.0f, -1.0f, 0.0f}) * leg.radius[3];
		legJoints.push_back(joints);
	}
	// 手足が無ければ、胴体の底を地面に置く
	float lowest = legJoints.empty() ? -halfHeight : legJoints.front().tip.y;
	for(const auto &joints : legJoints){
		lowest = std::min(lowest, joints.tip.y);
	}
	const V3 bodyCenter{0.0f, -lowest, 0.0f};

	// ボーン
	const int root = addBone(*data, "root", {}, -1);
	const int bodyBone = addBone(*data, "body", bodyCenter, root);

	// 胴体・顔(どちらも胴体のボーンに付く)
	addEgg(mesh, bodyCenter, halfWidth, halfHeight, halfLength, taper, body.slices, body.sides, bodyBone, Back);
	addFace(mesh, EggSurface(halfWidth, halfHeight, halfLength, taper), bodyCenter, spec.face, bodyBone);

	// 手足(左右1対ずつ)。ボーンは 付け根(胴体の子)→ひざ→足首→足先(足先の端)
	static constexpr const char *kJointNames[4] = {"hip", "knee", "ankle", "foot"};
	for(size_t l = 0; l < spec.legs.size(); ++l){
		const auto &leg = spec.legs[l];
		const auto &joints = legJoints[l];
		for(const float side : {1.0f, -1.0f}){
			const std::string prefix = leg.name + (side > 0.0f ? "_left_" : "_right_");
			std::vector<TubePoint> points(4);
			int parent = bodyBone;
			for(int j = 0; j < 4; ++j){
				const V3 position = bodyCenter + toModel(joints.joint[j], side);
				parent = addBone(*data, prefix + kJointNames[j], j < 3 ? position : bodyCenter + toModel(joints.tip, side), parent);
				points[static_cast<size_t>(j)] = {position, leg.radius[j], parent};
			}
			TubeStyle style;
			style.blend = spec.legMesh.blend;
			style.rings = spec.legMesh.rings;
			style.sides = spec.legMesh.sides;
			style.rootBone = bodyBone;
			style.inset = spec.legMesh.inset * leg.radius[0];
			addTube(mesh, points, style, Limb);
		}
	}

	// 材質ごとに、三角形を続けて並べる
	static constexpr const char *kMaterialNames[MaterialCount] = {"back", "belly", "limb", "face"};
	const float *colors[MaterialCount] = {spec.color.back, spec.color.belly, spec.color.legs, spec.color.face};
	for(int m = 0; m < MaterialCount; ++m){
		const auto &list = mesh.indices()[m];
		ModelMaterial material;
		material.name = kMaterialNames[m];
		std::copy(colors[m], colors[m] + 3, material.diffuse);
		material.specular = m == Face ? 0.6f : 0.25f;
		material.shininess = m == Face ? 40.0f : 12.0f;
		material.firstIndex = static_cast<uint32_t>(data->indices.size());
		material.indexCount = static_cast<uint32_t>(list.size());
		data->indices.insert(data->indices.end(), list.begin(), list.end());
		data->materials.push_back(std::move(material));
	}
	return data;
}

} // namespace model
