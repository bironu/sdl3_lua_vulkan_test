#include "model/SpringBones.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace model
{

namespace
{

struct V3
{
	float x, y, z;
};

V3 operator+(const V3 &a, const V3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3 &a, const V3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(const V3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3 &a, const V3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(const V3 &a) { return std::sqrt(dot(a, a)); }
V3 normalized(const V3 &a, const V3 &fallback)
{
	const float len = length(a);
	return len > 1e-9f ? a * (1.0f / len) : fallback;
}

struct M3
{
	float m[3][3];
};

M3 identityM3() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }

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

V3 rotate(const M3 &a, const V3 &v)
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

M3 fromMat34(const Mat34 &g)
{
	M3 r{};
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			r.m[i][j] = g.r[i][j];
		}
	}
	return r;
}

// 単位ベクトルaをbへ回す最短の回転
M3 shortestArc(const V3 &a, const V3 &b)
{
	const V3 axis = cross(a, b);
	const float d = std::clamp(dot(a, b), -1.0f, 1.0f);
	const float s = length(axis);
	if(s < 1e-6f){
		if(d > 0.0f){
			return identityM3();
		}
		const V3 helper = std::fabs(a.x) < 0.9f ? V3{1, 0, 0} : V3{0, 1, 0};
		const V3 perpendicular = normalized(cross(a, helper), V3{0, 0, 1});
		return fromQuat(Quat::fromAxisAngle({perpendicular.x, perpendicular.y, perpendicular.z}, 3.14159265f));
	}
	return fromQuat(Quat::fromAxisAngle({axis.x / s, axis.y / s, axis.z / s}, std::acos(d)));
}

V3 position(const Mat34 &g) { return {g.t[0], g.t[1], g.t[2]}; }

// 点pを、中心cと半径rの球の外へ押し出す
V3 pushOutOfSphere(const V3 &p, const V3 &c, float r)
{
	const V3 d = p - c;
	const float len = length(d);
	if(len >= r || len < 1e-9f){
		return p;
	}
	return c + d * (r / len);
}

} // namespace

std::unique_ptr<SpringBones> SpringBones::create(const ModelData &data, const Skeleton &skeleton)
{
	if(data.springChains.empty()){
		return nullptr;
	}
	std::unique_ptr<SpringBones> result(new SpringBones());
	result->colliders_ = data.springColliders;
	for(auto &c : result->colliders_){
		if(c.bone >= static_cast<int>(skeleton.boneCount())){
			c.bone = -1;
		}
	}
	size_t jointCount = 0;
	for(const auto &src : data.springChains){
		Chain chain;
		for(const int group : src.colliderGroups){
			if(group < 0 || group >= static_cast<int>(data.springColliderGroups.size())){
				continue;
			}
			for(const int collider : data.springColliderGroups[static_cast<size_t>(group)].colliders){
				if(collider >= 0 && collider < static_cast<int>(result->colliders_.size())
					&& std::find(chain.colliders.begin(), chain.colliders.end(), collider) == chain.colliders.end()){
					chain.colliders.push_back(collider);
				}
			}
		}
		const size_t simulated = src.virtualTail ? src.joints.size() : src.joints.size() - 1;
		for(size_t i = 0; i < simulated; ++i){
			Joint joint;
			joint.bone = src.joints[i].bone;
			joint.tailBone = i + 1 < src.joints.size() ? src.joints[i + 1].bone : -1;
			joint.setting = src.joints[i];
			joint.parentBone = skeleton.bone(joint.bone).parent;
			const float *bp = skeleton.bone(joint.bone).position;
			V3 direction;
			if(joint.tailBone >= 0){
				const float *tp = skeleton.bone(joint.tailBone).position;
				direction = {tp[0] - bp[0], tp[1] - bp[1], tp[2] - bp[2]};
			}
			else{
				// 仮想の先端: 親から自分への向きに、7cmだけ先
				if(joint.parentBone < 0){
					continue;
				}
				const float *pp = skeleton.bone(joint.parentBone).position;
				direction = {bp[0] - pp[0], bp[1] - pp[1], bp[2] - pp[2]};
				const float len = length(direction);
				if(len < 1e-6f){
					continue;
				}
				direction = direction * (0.07f / len);
			}
			joint.length = length(direction);
			if(joint.length < 1e-6f){
				continue; // 先端が根元と同じ位置では、向きが決まらない
			}
			// 先端の向きを、ボーンの休止ポーズの座標系で持つ
			const V3 local = rotate(transpose(fromQuat(skeleton.restGlobalRotation(joint.bone))), direction * (1.0f / joint.length));
			joint.localDirection[0] = local.x;
			joint.localDirection[1] = local.y;
			joint.localDirection[2] = local.z;
			chain.joints.push_back(joint);
			++jointCount;
		}
		if(!chain.joints.empty()){
			result->chains_.push_back(std::move(chain));
		}
	}
	if(result->chains_.empty()){
		return nullptr;
	}
	result->reset(skeleton);
	SDL_Log("SpringBones: %zu chains, %zu joints, %zu colliders", result->chains_.size(), jointCount, result->colliders_.size());
	return result;
}

void SpringBones::reset(const Skeleton &skeleton)
{
	accumulator_ = 0.0f;
	for(auto &chain : chains_){
		for(auto &joint : chain.joints){
			V3 tail;
			if(joint.tailBone >= 0){
				tail = position(skeleton.globalTransform(joint.tailBone));
			}
			else{
				// 仮想の先端: 根元の位置 + (親の姿勢で決まる、元の向き) x 長さ
				const ModelBone &bone = skeleton.bone(joint.bone);
				const M3 parentRotation = joint.parentBone >= 0 ? fromMat34(skeleton.globalTransform(joint.parentBone)) : identityM3();
				const M3 restRotation = fromQuat(Quat{bone.restRotation[0], bone.restRotation[1], bone.restRotation[2], bone.restRotation[3]}.normalized());
				const V3 u = rotate(mul(parentRotation, restRotation), V3{joint.localDirection[0], joint.localDirection[1], joint.localDirection[2]});
				tail = position(skeleton.globalTransform(joint.bone)) + u * joint.length;
			}
			joint.current[0] = joint.previous[0] = tail.x;
			joint.current[1] = joint.previous[1] = tail.y;
			joint.current[2] = joint.previous[2] = tail.z;
		}
	}
}

void SpringBones::pass(Skeleton &skeleton, float dt, bool commit, std::vector<int> &bones, std::vector<Mat34> &globals)
{
	// このパスで姿勢を求めたジョイントのボーン(親がこれなら、親の動いた姿勢に従う。分かれた枝も含む)
	const size_t boneCount = skeleton.boneCount();
	std::vector<char> simulated(boneCount, 0);
	std::vector<V3> simulatedPosition(boneCount);
	std::vector<M3> simulatedRotation(boneCount);
	for(auto &chain : chains_){
		V3 head{};
		M3 parentRotation = identityM3();
		for(auto &joint : chain.joints){
			const ModelBone &bone = skeleton.bone(joint.bone);
			if(bone.parent >= 0 && simulated[static_cast<size_t>(bone.parent)]){
				const Vec3 &t = skeleton.restLocalTranslation(joint.bone);
				const size_t p = static_cast<size_t>(bone.parent);
				head = simulatedPosition[p] + rotate(simulatedRotation[p], V3{t.x, t.y, t.z});
				parentRotation = simulatedRotation[p];
			}
			else{
				head = position(skeleton.globalTransform(joint.bone));
				parentRotation = joint.parentBone >= 0 ? fromMat34(skeleton.globalTransform(joint.parentBone)) : identityM3();
			}
			const M3 restRotation = fromQuat(Quat{bone.restRotation[0], bone.restRotation[1], bone.restRotation[2], bone.restRotation[3]}.normalized());
			const M3 noSpring = mul(parentRotation, restRotation);
			const V3 u = rotate(noSpring, V3{joint.localDirection[0], joint.localDirection[1], joint.localDirection[2]});

			const V3 current = {joint.current[0], joint.current[1], joint.current[2]};
			const V3 previous = {joint.previous[0], joint.previous[1], joint.previous[2]};
			V3 next = current;
			if(commit){
				const V3 gravity = {joint.setting.gravityDir[0], joint.setting.gravityDir[1], joint.setting.gravityDir[2]};
				next = current + (current - previous) * (1.0f - joint.setting.dragForce) + u * (joint.setting.stiffness * dt)
					+ gravity * (joint.setting.gravityPower * dt);
			}
			next = head + normalized(next - head, u) * joint.length;
			// コライダー(球とカプセル)の外へ押し出して、長さを保つ
			for(const int index : chain.colliders){
				const SpringCollider &collider = colliders_[static_cast<size_t>(index)];
				if(collider.bone < 0){
					continue;
				}
				const Mat34 &g = skeleton.globalTransform(collider.bone);
				const M3 gr = fromMat34(g);
				const V3 a = position(g) + rotate(gr, V3{collider.offset[0], collider.offset[1], collider.offset[2]});
				const float radius = collider.radius + joint.setting.hitRadius;
				V3 center = a;
				if(collider.shape == SpringCollider::Shape::Capsule){
					const V3 b = position(g) + rotate(gr, V3{collider.tail[0], collider.tail[1], collider.tail[2]});
					const V3 ab = b - a;
					const float len2 = dot(ab, ab);
					const float t = len2 > 1e-12f ? std::clamp(dot(next - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
					center = a + ab * t;
				}
				const V3 pushed = pushOutOfSphere(next, center, radius);
				if(length(pushed - next) > 0.0f){
					next = head + normalized(pushed - head, u) * joint.length;
				}
			}
			if(commit){
				joint.previous[0] = joint.current[0]; joint.previous[1] = joint.current[1]; joint.previous[2] = joint.current[2];
				joint.current[0] = next.x; joint.current[1] = next.y; joint.current[2] = next.z;
			}

			// ボーンの向き: 元の向きから、先端の向きへ回す
			const V3 direction = normalized(next - head, u);
			const M3 rotation = mul(shortestArc(u, direction), noSpring);
			Mat34 out;
			for(int i = 0; i < 3; ++i){
				for(int j = 0; j < 3; ++j){
					out.r[i][j] = rotation.m[i][j];
				}
			}
			out.t[0] = head.x; out.t[1] = head.y; out.t[2] = head.z;
			bones.push_back(joint.bone);
			globals.push_back(out);
			simulated[static_cast<size_t>(joint.bone)] = 1;
			simulatedPosition[static_cast<size_t>(joint.bone)] = head;
			simulatedRotation[static_cast<size_t>(joint.bone)] = rotation;
		}
	}
}

void SpringBones::step(Skeleton &skeleton, float dt)
{
	std::vector<int> bones;
	std::vector<Mat34> globals;
	if(dt > 0.0f){
		// 固定の刻みで進める(フレームレートによらず、揺れ方が変わらないように)。溜めすぎない
		constexpr float kFixedStep = 1.0f / 60.0f;
		accumulator_ = std::min(accumulator_ + dt, kFixedStep * 5.0f);
		while(accumulator_ >= kFixedStep){
			bones.clear();
			globals.clear();
			pass(skeleton, kFixedStep, true, bones, globals);
			accumulator_ -= kFixedStep;
		}
	}
	// 現在の姿勢(動いたボーンの上)に合わせた最終的な姿勢(状態は進めない)
	bones.clear();
	globals.clear();
	pass(skeleton, 0.0f, false, bones, globals);
	skeleton.applyPhysics(bones, globals);
}

} // namespace model
