#include "model/Retarget.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace model
{

namespace
{

struct M3
{
	float m[3][3];
};

M3 identityM3()
{
	return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
}

M3 mul(const M3 &a, const M3 &b)
{
	M3 r{};
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
		}
	}
	return r;
}

M3 transpose(const M3 &a)
{
	M3 r{};
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			r.m[i][j] = a.m[j][i];
		}
	}
	return r;
}

Vec3 rotate(const M3 &a, const Vec3 &v)
{
	return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
		a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
		a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

M3 fromQuat(const Quat &q)
{
	const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	return {{{1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy)},
		{2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx)},
		{2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)}}};
}

M3 fromMat34(const Mat34 &m)
{
	M3 r{};
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			r.m[i][j] = m.r[i][j];
		}
	}
	return r;
}

Quat toQuat(const M3 &r)
{
	Quat q;
	const float trace = r.m[0][0] + r.m[1][1] + r.m[2][2];
	if(trace > 0.0f){
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		q.w = 0.25f * s;
		q.x = (r.m[2][1] - r.m[1][2]) / s;
		q.y = (r.m[0][2] - r.m[2][0]) / s;
		q.z = (r.m[1][0] - r.m[0][1]) / s;
	}
	else if(r.m[0][0] > r.m[1][1] && r.m[0][0] > r.m[2][2]){
		const float s = std::sqrt(1.0f + r.m[0][0] - r.m[1][1] - r.m[2][2]) * 2.0f;
		q.w = (r.m[2][1] - r.m[1][2]) / s;
		q.x = 0.25f * s;
		q.y = (r.m[0][1] + r.m[1][0]) / s;
		q.z = (r.m[0][2] + r.m[2][0]) / s;
	}
	else if(r.m[1][1] > r.m[2][2]){
		const float s = std::sqrt(1.0f + r.m[1][1] - r.m[0][0] - r.m[2][2]) * 2.0f;
		q.w = (r.m[0][2] - r.m[2][0]) / s;
		q.x = (r.m[0][1] + r.m[1][0]) / s;
		q.y = 0.25f * s;
		q.z = (r.m[1][2] + r.m[2][1]) / s;
	}
	else{
		const float s = std::sqrt(1.0f + r.m[2][2] - r.m[0][0] - r.m[1][1]) * 2.0f;
		q.w = (r.m[1][0] - r.m[0][1]) / s;
		q.x = (r.m[0][2] + r.m[2][0]) / s;
		q.y = (r.m[1][2] + r.m[2][1]) / s;
		q.z = 0.25f * s;
	}
	return q.normalized();
}

float length(const Vec3 &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// 単位ベクトルaをbへ回す最短の回転
M3 shortestArc(Vec3 a, Vec3 b)
{
	const float la = length(a), lb = length(b);
	a = {a.x / la, a.y / la, a.z / la};
	b = {b.x / lb, b.y / lb, b.z / lb};
	const Vec3 axis = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	const float d = std::clamp(a.x * b.x + a.y * b.y + a.z * b.z, -1.0f, 1.0f);
	const float s = length(axis);
	if(s < 1e-6f){
		if(d > 0.0f){
			return identityM3();
		}
		// 真逆: aに垂直な任意の軸で180度
		const Vec3 helper = std::fabs(a.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
		Vec3 perpendicular = {a.y * helper.z - a.z * helper.y, a.z * helper.x - a.x * helper.z, a.x * helper.y - a.y * helper.x};
		const float lp = length(perpendicular);
		perpendicular = {perpendicular.x / lp, perpendicular.y / lp, perpendicular.z / lp};
		return fromQuat(Quat::fromAxisAngle(perpendicular, 3.14159265f));
	}
	return fromQuat(Quat::fromAxisAngle({axis.x / s, axis.y / s, axis.z / s}, std::acos(d)));
}

// 対応表: (VRMのヒューマノイド名, MMDのボーン名, 向きを合わせる先のVRMの名前, 同じくMMDの名前)。
// 向きを合わせる先が空のボーン(指先など)は、1つ前の項目(親)の補正を使う。並びは、親が先
struct Entry
{
	const char *humanoid;
	const char *mmd;
	const char *aimHumanoid;
	const char *aimMmd;
};

void appendSide(std::vector<Entry> &table, const char *side, const char *mmdSide, std::vector<std::string> &storage)
{
	// 文字列を保持する(Entryはポインタだけ持つので、storageが寿命を持つ)
	auto keep = [&](const std::string &s){ storage.push_back(s); return storage.back().c_str(); };
	const std::string h = side, m = mmdSide;
	table.push_back({keep(h + "Shoulder"), keep(m + "肩"), keep(h + "UpperArm"), keep(m + "腕")});
	table.push_back({keep(h + "UpperArm"), keep(m + "腕"), keep(h + "LowerArm"), keep(m + "ひじ")});
	table.push_back({keep(h + "LowerArm"), keep(m + "ひじ"), keep(h + "Hand"), keep(m + "手首")});
	table.push_back({keep(h + "Hand"), keep(m + "手首"), keep(h + "MiddleProximal"), keep(m + "中指１")});
	table.push_back({keep(h + "ThumbMetacarpal"), keep(m + "親指０"), keep(h + "ThumbProximal"), keep(m + "親指１")});
	table.push_back({keep(h + "ThumbProximal"), keep(m + "親指１"), keep(h + "ThumbDistal"), keep(m + "親指２")});
	table.push_back({keep(h + "ThumbDistal"), keep(m + "親指２"), "", ""});
	const struct { const char *name; const char *mmd; } fingers[] = {{"Index", "人指"}, {"Middle", "中指"}, {"Ring", "薬指"}, {"Little", "小指"}};
	for(const auto &f : fingers){
		table.push_back({keep(h + f.name + "Proximal"), keep(m + f.mmd + "１"), keep(h + f.name + "Intermediate"), keep(m + f.mmd + "２")});
		table.push_back({keep(h + f.name + "Intermediate"), keep(m + f.mmd + "２"), keep(h + f.name + "Distal"), keep(m + f.mmd + "３")});
		table.push_back({keep(h + f.name + "Distal"), keep(m + f.mmd + "３"), "", ""});
	}
	table.push_back({keep(h + "UpperLeg"), keep(m + "足"), keep(h + "LowerLeg"), keep(m + "ひざ")});
	table.push_back({keep(h + "LowerLeg"), keep(m + "ひざ"), keep(h + "Foot"), keep(m + "足首")});
	table.push_back({keep(h + "Foot"), keep(m + "足首"), keep(h + "Toes"), keep(m + "つま先")});
	table.push_back({keep(h + "Toes"), keep(m + "つま先"), "", ""});
}

} // namespace

std::unique_ptr<Retargeter> Retargeter::create(const Skeleton &source, const Skeleton &target,
	const std::vector<std::pair<std::string, int>> &humanoid)
{
	std::unordered_map<std::string, int> humanoidIndex;
	for(const auto &entry : humanoid){
		humanoidIndex.emplace(entry.first, entry.second);
	}
	auto targetBone = [&](const char *name) -> int {
		const auto it = humanoidIndex.find(name);
		return it == humanoidIndex.end() ? -1 : it->second;
	};

	std::vector<std::string> storage;
	storage.reserve(1024); // keep()が返すポインタが、再確保で無効にならないように
	std::vector<Entry> table = {
		{"hips", "下半身", "spine", "上半身"},
		{"spine", "上半身", "chest", "上半身2"},
		{"chest", "上半身2", "neck", "首"},
		{"neck", "首", "head", "頭"},
		{"head", "頭", "", ""},
	};
	appendSide(table, "left", "左", storage);
	appendSide(table, "right", "右", storage);

	std::unique_ptr<Retargeter> result(new Retargeter());
	result->targetToPair_.assign(target.boneCount(), -1);
	result->targetHumanoid_.assign(target.boneCount(), std::string());
	M3 previous = identityM3();
	for(const Entry &e : table){
		const int t = targetBone(e.humanoid);
		const int s = source.findBone(e.mmd);
		const int aimT = e.aimHumanoid[0] ? targetBone(e.aimHumanoid) : -1;
		const int aimS = e.aimMmd[0] ? source.findBone(e.aimMmd) : -1;
		if(t < 0 || s < 0){
			// 対応づけできない項目の補正は、子の「1つ前の補正」として引き継がない(単位に戻す)
			previous = identityM3();
			continue;
		}
		Pair pair;
		pair.source = s;
		pair.target = t;
		M3 correction = previous; // 向きを合わせる先が無いときは、親の補正を使う
		if(aimT >= 0 && aimS >= 0){
			const float *tp = target.bone(t).position, *ta = target.bone(aimT).position;
			const float *sp = source.bone(s).position, *sa = source.bone(aimS).position;
			const Vec3 dt = {ta[0] - tp[0], ta[1] - tp[1], ta[2] - tp[2]};
			const Vec3 ds = {sa[0] - sp[0], sa[1] - sp[1], sa[2] - sp[2]};
			if(length(dt) > 1e-5f && length(ds) > 1e-5f){
				correction = shortestArc(dt, ds);
			}
		}
		for(int i = 0; i < 3; ++i){
			for(int j = 0; j < 3; ++j){
				pair.correction[i][j] = correction.m[i][j];
			}
		}
		previous = correction;
		result->targetToPair_[static_cast<size_t>(t)] = static_cast<int>(result->pairs_.size());
		result->targetHumanoid_[static_cast<size_t>(t)] = e.humanoid;
		if(std::strcmp(e.humanoid, "hips") == 0){
			result->hipsPair_ = static_cast<int>(result->pairs_.size());
			for(int k = 0; k < 3; ++k){ result->hipsRest_[k] = source.bone(s).position[k]; }
		}
		result->pairs_.push_back(pair);
	}
	if(result->hipsPair_ < 0 || result->pairs_.size() < 5){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Retarget: could not map the base bones (%zu bones mapped)", result->pairs_.size());
		return nullptr;
	}

	// 移動量の拡大縮小: 足の付け根の高さの比(MMD単位 → メートル)
	const int sLeg = source.findBone("左足");
	const int tLeg = targetBone("leftUpperLeg");
	result->translationScale_ = (sLeg >= 0 && tLeg >= 0 && source.bone(sLeg).position[1] > 1e-3f)
		? target.bone(tLeg).position[1] / source.bone(sLeg).position[1] : 0.08f;
	SDL_Log("Retarget: %zu bones mapped, translation scale %.4f", result->pairs_.size(), result->translationScale_);
	return result;
}

void Retargeter::computePose(const Skeleton &source, const Skeleton &target, RetargetPose &out) const
{
	(void)target;
	const size_t count = targetToPair_.size();
	out.worldDelta.assign(count, Quat{});
	out.mapped.assign(count, false);
	out.hipsTranslation = Vec3{};
	out.hipsBone = -1;
	for(size_t i = 0; i < count; ++i){
		const int p = targetToPair_[i];
		if(p < 0){
			continue;
		}
		const Pair &pair = pairs_[static_cast<size_t>(p)];
		// MMD側の、休止ポーズからのワールド空間での回転 Q = (現在のグローバルな向き) * (休止ポーズの向きの逆)
		const M3 current = fromMat34(source.globalTransform(pair.source));
		const M3 sourceRest = fromQuat(source.restGlobalRotation(pair.source));
		const M3 q = mul(current, transpose(sourceRest));
		M3 correction;
		std::memcpy(correction.m, pair.correction, sizeof(correction.m));
		out.worldDelta[i] = toQuat(mul(q, correction));
		out.mapped[i] = true;
		if(p == hipsPair_){
			const Mat34 &g = source.globalTransform(pair.source);
			out.hipsBone = static_cast<int>(i);
			out.hipsTranslation = {(g.t[0] - hipsRest_[0]) * translationScale_, (g.t[1] - hipsRest_[1]) * translationScale_,
				(g.t[2] - hipsRest_[2]) * translationScale_};
		}
	}
}

void Retargeter::applyPose(const RetargetPose &pose, Skeleton &target)
{
	const size_t count = target.boneCount();
	std::vector<M3> final(count, identityM3()); // VRMの各ボーンの、最終的なグローバルな向き(親が先)
	for(size_t i = 0; i < count; ++i){
		const ModelBone &bone = target.bone(static_cast<int>(i));
		const M3 parent = bone.parent >= 0 ? final[static_cast<size_t>(bone.parent)] : identityM3();
		const M3 restLocal = fromQuat(Quat{bone.restRotation[0], bone.restRotation[1], bone.restRotation[2], bone.restRotation[3]}.normalized());
		if(i >= pose.mapped.size() || !pose.mapped[i]){
			target.setBoneRotation(static_cast<int>(i), Quat{});
			target.setBoneTranslation(static_cast<int>(i), Vec3{});
			final[i] = mul(parent, restLocal);
			continue;
		}
		const M3 targetRest = fromQuat(target.restGlobalRotation(static_cast<int>(i)));
		const M3 desired = mul(fromQuat(pose.worldDelta[i]), targetRest);
		// 局所の回転 = 親の向きの逆 * 目標の向き。アニメーション分は、休止ポーズの局所の回転を除く
		const M3 local = mul(transpose(parent), desired);
		target.setBoneRotation(static_cast<int>(i), toQuat(mul(transpose(restLocal), local)));
		final[i] = desired;
		if(static_cast<int>(i) == pose.hipsBone){
			target.setBoneTranslation(static_cast<int>(i), rotate(transpose(parent), pose.hipsTranslation));
		}
		else{
			target.setBoneTranslation(static_cast<int>(i), Vec3{});
		}
	}
}

void Retargeter::apply(const Skeleton &source, Skeleton &target) const
{
	RetargetPose pose;
	computePose(source, target, pose);
	applyPose(pose, target);
}

} // namespace model
