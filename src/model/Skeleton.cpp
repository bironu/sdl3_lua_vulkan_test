#include "model/Skeleton.h"
#include <algorithm>
#include <cmath>

namespace model
{

namespace
{

constexpr float kEpsilon = 1e-6f;

Vec3 operator+(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3 &a, const Vec3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(const Vec3 &a) { return std::sqrt(dot(a, a)); }

// 3x3回転行列(列ベクトルに左から掛ける)
struct Mat3
{
	float m[3][3];
};

Mat3 toMat3(const Quat &q)
{
	const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	return {{{1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy)},
		{2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx)},
		{2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)}}};
}

// 剛体変換の合成(parent * local)
Mat34 compose(const Mat34 &p, const Mat3 &lr, const Vec3 &lt)
{
	Mat34 out;
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			out.r[i][j] = p.r[i][0] * lr.m[0][j] + p.r[i][1] * lr.m[1][j] + p.r[i][2] * lr.m[2][j];
		}
		out.t[i] = p.r[i][0] * lt.x + p.r[i][1] * lt.y + p.r[i][2] * lt.z + p.t[i];
	}
	return out;
}

// 剛体変換の逆を点に適用: R^T (p - t)
Vec3 inverseTransformPoint(const Mat34 &m, const Vec3 &p)
{
	const Vec3 d = {p.x - m.t[0], p.y - m.t[1], p.z - m.t[2]};
	return {m.r[0][0] * d.x + m.r[1][0] * d.y + m.r[2][0] * d.z,
		m.r[0][1] * d.x + m.r[1][1] * d.y + m.r[2][1] * d.z,
		m.r[0][2] * d.x + m.r[1][2] * d.y + m.r[2][2] * d.z};
}

Vec3 normalizedOrZero(const Vec3 &v)
{
	const float len = length(v);
	return len > kEpsilon ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{};
}

// 回転をZXY順(R = Ry * Rx * Rz)のオイラー角に分解する(MMDのIK回転制限の座標)
Vec3 toEulerZXY(const Mat3 &r)
{
	Vec3 e;
	const float sx = -r.m[1][2];
	e.x = std::asin(std::clamp(sx, -1.0f, 1.0f));
	if(std::fabs(std::cos(e.x)) > 1e-4f){
		e.y = std::atan2(r.m[0][2], r.m[2][2]);
		e.z = std::atan2(r.m[1][0], r.m[1][1]);
	}
	else{ // ジンバルロック: Zを0とみなす
		e.y = std::atan2(-r.m[2][0], r.m[0][0]);
		e.z = 0.0f;
	}
	return e;
}

Quat fromEulerZXY(const Vec3 &e)
{
	const Quat qy = Quat::fromAxisAngle({0, 1, 0}, e.y);
	const Quat qx = Quat::fromAxisAngle({1, 0, 0}, e.x);
	const Quat qz = Quat::fromAxisAngle({0, 0, 1}, e.z);
	return qy * qx * qz;
}

} // namespace

// ---- Quat ----

Quat Quat::fromAxisAngle(const Vec3 &axis, float radians)
{
	const float half = radians * 0.5f;
	const float s = std::sin(half);
	return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

Quat Quat::operator*(const Quat &r) const
{
	return {w * r.x + x * r.w + y * r.z - z * r.y,
		w * r.y - x * r.z + y * r.w + z * r.x,
		w * r.z + x * r.y - y * r.x + z * r.w,
		w * r.w - x * r.x - y * r.y - z * r.z};
}

Quat Quat::normalized() const
{
	const float len = std::sqrt(x * x + y * y + z * z + w * w);
	return len > kEpsilon ? Quat{x / len, y / len, z / len, w / len} : Quat{};
}

Quat Quat::slerp(const Quat &a, const Quat &b, float t)
{
	float cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	Quat end = b;
	if(cosom < 0.0f){ // 短い側の弧を通る
		cosom = -cosom;
		end = {-b.x, -b.y, -b.z, -b.w};
	}
	float s0, s1;
	if(cosom < 0.9999f){
		const float omega = std::acos(cosom);
		const float inv = 1.0f / std::sin(omega);
		s0 = std::sin((1.0f - t) * omega) * inv;
		s1 = std::sin(t * omega) * inv;
	}
	else{
		s0 = 1.0f - t;
		s1 = t;
	}
	return Quat{s0 * a.x + s1 * end.x, s0 * a.y + s1 * end.y, s0 * a.z + s1 * end.z, s0 * a.w + s1 * end.w}.normalized();
}

// ---- Skeleton ----

Skeleton::Skeleton(const std::vector<ModelBone> &bones)
	: bones_(bones)
	, state_(bones.size())
	, skin_(bones.size())
	, ikBoneEnabled_(bones.size(), true)
{
	order_.resize(bones_.size());
	for(size_t i = 0; i < order_.size(); ++i){
		order_[i] = static_cast<int>(i);
		nameToIndex_.emplace(bones_[i].name, static_cast<int>(i)); // 同名は最初のものを優先
	}
	// 変形階層→番号の順
	std::stable_sort(order_.begin(), order_.end(), [this](int a, int b){ return bones_[a].layer < bones_[b].layer; });
	// 休止ポーズでの、ボーンのグローバルな回転と、親の座標系での親からの位置(親が先になるよう、親をたどって求める)
	restGlobalRotation_.assign(bones_.size(), Quat());
	restLocalTranslation_.assign(bones_.size(), Vec3());
	std::vector<bool> done(bones_.size(), false);
	for(size_t start = 0; start < bones_.size(); ++start){
		std::vector<int> chain;
		for(int i = static_cast<int>(start); i >= 0 && i < static_cast<int>(bones_.size()) && !done[i]; i = bones_[i].parent){
			chain.push_back(i);
			if(chain.size() > bones_.size()){ break; } // 親が循環していても止まる
		}
		for(auto it = chain.rbegin(); it != chain.rend(); ++it){
			const int i = *it;
			const ModelBone &b = bones_[i];
			const Quat local{b.restRotation[0], b.restRotation[1], b.restRotation[2], b.restRotation[3]};
			Vec3 offset = {b.position[0], b.position[1], b.position[2]};
			if(b.parent >= 0 && b.parent < static_cast<int>(bones_.size())){
				const ModelBone &p = bones_[b.parent];
				restGlobalRotation_[i] = (restGlobalRotation_[b.parent] * local).normalized();
				offset = offset - Vec3{p.position[0], p.position[1], p.position[2]};
				// 親の座標系へ: 親のグローバルな回転の逆で回す
				const Mat3 inverse = toMat3(restGlobalRotation_[b.parent].conjugate());
				restLocalTranslation_[i] = {inverse.m[0][0] * offset.x + inverse.m[0][1] * offset.y + inverse.m[0][2] * offset.z,
					inverse.m[1][0] * offset.x + inverse.m[1][1] * offset.y + inverse.m[1][2] * offset.z,
					inverse.m[2][0] * offset.x + inverse.m[2][1] * offset.y + inverse.m[2][2] * offset.z};
			}
			else{
				restGlobalRotation_[i] = local.normalized();
				restLocalTranslation_[i] = offset;
			}
			done[i] = true;
		}
	}
	resetPose();
	update();
}

int Skeleton::findBone(const std::string &name) const
{
	const auto i = nameToIndex_.find(name);
	return i != nameToIndex_.end() ? i->second : -1;
}

void Skeleton::setIkEnabled(int ikBone, bool enabled)
{
	if(ikBone >= 0 && ikBone < static_cast<int>(ikBoneEnabled_.size())){
		ikBoneEnabled_[ikBone] = enabled;
	}
}

void Skeleton::resetPose()
{
	for(auto &s : state_){
		s.animRotation = {};
		s.animTranslation = {};
		s.ikRotation = {};
	}
}

void Skeleton::setBoneRotation(int bone, const Quat &rotation)
{
	if(bone >= 0 && bone < static_cast<int>(state_.size())){
		state_[bone].animRotation = rotation;
	}
}

void Skeleton::setBoneTranslation(int bone, const Vec3 &translation)
{
	if(bone >= 0 && bone < static_cast<int>(state_.size())){
		state_[bone].animTranslation = translation;
	}
}

Vec3 Skeleton::globalPosition(int bone) const
{
	const Mat34 &g = state_[bone].global;
	return {g.t[0], g.t[1], g.t[2]};
}

// 1本のボーンの局所姿勢(付与・IK込み)とグローバル姿勢を、親のグローバル姿勢から更新する
void Skeleton::updateBone(int index)
{
	const ModelBone &b = bones_[index];
	State &s = state_[index];

	// 付与: 付与親の局所の回転/移動を、割合を掛けて受け取る
	Quat rotation = s.animRotation;
	Vec3 translation = s.animTranslation;
	if(b.appendParent >= 0 && b.appendParent < static_cast<int>(state_.size())){
		const State &p = state_[b.appendParent];
		if(b.flags & ModelBone::AppendRotation){
			// 割合が負なら逆回転。付与親の最終的な局所回転(付与・IK込み)を使う
			const Quat src = b.appendRatio >= 0.0f ? p.localRotation : p.localRotation.conjugate();
			rotation = rotation * Quat::slerp(Quat{}, src, std::fabs(b.appendRatio));
		}
		if(b.flags & ModelBone::AppendTranslation){
			// 付与親の移動(アニメーション分)に割合を掛ける
			translation = translation + Vec3{p.animTranslation.x * b.appendRatio, p.animTranslation.y * b.appendRatio, p.animTranslation.z * b.appendRatio};
		}
	}
	s.localRotation = (rotation * s.ikRotation).normalized();

	// 局所の移動 = 休止ポーズでの親からの位置 + アニメーションの移動
	s.localTranslation = restLocalTranslation_[index] + translation;

	// 休止ポーズの局所の回転(PMXは回転なし)の上に、アニメーション・付与・IKの回転を重ねる
	const Quat restRotation{b.restRotation[0], b.restRotation[1], b.restRotation[2], b.restRotation[3]};
	const Mat3 rotation3 = toMat3((restRotation * s.localRotation).normalized());
	if(b.parent >= 0){
		s.global = compose(state_[b.parent].global, rotation3, s.localTranslation);
	}
	else{
		Mat34 identity;
		s.global = compose(identity, rotation3, s.localTranslation);
	}
}

void Skeleton::updateAllBones()
{
	for(int index : order_){
		updateBone(index);
	}
}

// IK回転制限: リンクの最終的な局所回転(アニメ+IK)のオイラー角を制限範囲に収め、ikRotationを逆算し直す
void Skeleton::applyIkLimits(const ModelBone::IkLink &link, State &s) const
{
	if(!link.hasLimit){
		return;
	}
	const Quat total = s.animRotation * s.ikRotation;
	Vec3 e = toEulerZXY(toMat3(total));
	e.x = std::clamp(e.x, link.limitMin[0], link.limitMax[0]);
	e.y = std::clamp(e.y, link.limitMin[1], link.limitMax[1]);
	e.z = std::clamp(e.z, link.limitMin[2], link.limitMax[2]);
	// total = anim * ik → ik = anim^-1 * total
	s.ikRotation = (s.animRotation.conjugate() * fromEulerZXY(e)).normalized();
}

// CCD法でIKを解く。ikBoneの位置を目標にして、ターゲットボーン(足首など)がそこへ届くように、
// リンクボーン(ひざ・もも)を、ターゲットに近い順に少しずつ回す
void Skeleton::solveIk(int ikBone)
{
	const ModelBone &ik = bones_[ikBone];
	if(ik.ikTarget < 0 || ik.ikLinks.empty()){
		return;
	}
	for(auto &link : ik.ikLinks){
		state_[link.bone].ikRotation = {};
	}
	updateAllBones();

	const Vec3 goal = globalPosition(ikBone);
	for(int iteration = 0; iteration < ik.ikLoop; ++iteration){
		for(size_t i = 0; i < ik.ikLinks.size(); ++i){
			const auto &link = ik.ikLinks[i];
			if(link.bone < 0){
				continue;
			}
			State &s = state_[link.bone];
			const Vec3 effector = globalPosition(ik.ikTarget);
			// リンクの局所座標で、ターゲット(effector)と目標(goal)の向きを合わせる回転を求める
			const Vec3 localEffector = normalizedOrZero(inverseTransformPoint(s.global, effector));
			const Vec3 localGoal = normalizedOrZero(inverseTransformPoint(s.global, goal));
			const float d = std::clamp(dot(localEffector, localGoal), -1.0f, 1.0f);
			if(d > 1.0f - kEpsilon){
				continue; // もう届いている
			}
			float angle = std::acos(d);
			angle = std::min(angle, ik.ikLimit);
			const Vec3 axis = normalizedOrZero(cross(localEffector, localGoal));
			if(length(axis) < kEpsilon){
				continue;
			}
			s.ikRotation = (s.ikRotation * Quat::fromAxisAngle(axis, angle)).normalized();
			applyIkLimits(link, s);
			updateAllBones();
		}
	}
}

void Skeleton::update()
{
	// 付与・FKを評価順に解く。IKボーンに来たらIKを解き、その後ろは姿勢が変わるので全体を更新し直す
	updateAllBones();
	if(ikEnabled_){
		for(int index : order_){
			if((bones_[index].flags & ModelBone::IK) && ikBoneEnabled_[index]){
				solveIk(index);
			}
		}
	}

	computeSkinMatrices();
}

void Skeleton::applyPhysics(const std::vector<int> &bones, const std::vector<Mat34> &globals)
{
	std::vector<bool> overridden(bones_.size(), false);
	for(size_t i = 0; i < bones.size() && i < globals.size(); ++i){
		const int bone = bones[i];
		if(bone >= 0 && bone < static_cast<int>(state_.size())){
			state_[bone].global = globals[i];
			overridden[bone] = true;
		}
	}
	for(int index : order_){
		if(!overridden[index]){
			updateBone(index); // IKの結果(ikRotation)はそのまま使い、親が物理演算で動いた分だけ更新する
		}
	}
	computeSkinMatrices();
}

void Skeleton::computeSkinMatrices()
{
	// スキニング行列 = グローバル姿勢 × 休止ポーズの逆(休止ポーズ = 回転 restGlobalRotation_ と平行移動 position)
	for(size_t i = 0; i < bones_.size(); ++i){
		const Mat34 &g = state_[i].global;
		Mat34 &m = skin_[i];
		const float *rest = bones_[i].position;
		const Mat3 restRotation = toMat3(restGlobalRotation_[i]);
		for(int row = 0; row < 3; ++row){
			for(int col = 0; col < 3; ++col){
				// R = g.R * restR^T
				m.r[row][col] = g.r[row][0] * restRotation.m[col][0] + g.r[row][1] * restRotation.m[col][1] + g.r[row][2] * restRotation.m[col][2];
			}
		}
		for(int row = 0; row < 3; ++row){
			m.t[row] = g.t[row] - (m.r[row][0] * rest[0] + m.r[row][1] * rest[1] + m.r[row][2] * rest[2]);
		}
	}
}

} // namespace model
