#include "scene/game/CreatureAnimator.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>

namespace game
{

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;
constexpr size_t kMaxHullPoints = 256; // 胴体のいちばん低い所を調べる頂点の数の上限(間引く)

model::Vec3 operator+(const model::Vec3 &a, const model::Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
model::Vec3 operator-(const model::Vec3 &a, const model::Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
model::Vec3 operator*(const model::Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
model::Vec3 cross(const model::Vec3 &a, const model::Vec3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(const model::Vec3 &a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
// 長さ1にする。短すぎれば fallback
model::Vec3 normalize(const model::Vec3 &a, const model::Vec3 &fallback)
{
	const float l = length(a);
	return l > 1e-6f ? model::Vec3{a.x / l, a.y / l, a.z / l} : fallback;
}
model::Vec3 restPosition(const model::Skeleton &skeleton, int bone)
{
	const auto &p = skeleton.bone(bone).position;
	return {p[0], p[1], p[2]};
}

float smoothstep(float edge0, float edge1, float x)
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// 時刻 t の、start から長さ span の区間の中の進み具合(0〜1)
float progress(float t, float start, float span)
{
	return span > 1e-4f ? std::clamp((t - start) / span, 0.0f, 1.0f) : (t >= start ? 1.0f : 0.0f);
}

float fract(float x) { return x - std::floor(x); }

// 地面に着いてから tau 秒の、潰れの量(+で縦に潰れ、-で縦に伸びる)。減衰するバネ: 着いた瞬間に amount、半周期ごとに逆向きに bounce 倍、duration で0
float springSquash(const CreatureMotion::Squash &squash, float tau)
{
	if(tau < 0.0f || tau >= squash.duration || squash.amount == 0.0f){
		return 0.0f;
	}
	const float half = 0.3f * squash.duration;
	const float decay = -std::log(std::clamp(squash.bounce, 0.01f, 0.95f)) / half;
	const float fade = 1.0f - smoothstep(0.7f * squash.duration, squash.duration, tau);
	return squash.amount * std::exp(-decay * tau) * std::cos(kPi * tau / half) * fade;
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }

const model::Vec3 kForward{0.0f, 0.0f, -1.0f}; // モデルの正面(元の座標系の-Z)
const model::Vec3 kUp{0.0f, 1.0f, 0.0f};
const model::Vec3 kAxisX{1.0f, 0.0f, 0.0f};
const model::Vec3 kAxisZ{0.0f, 0.0f, 1.0f};
}

CreatureAnimator::CreatureAnimator(const model::Skeleton &skeleton, const std::vector<model::ModelVertex> &vertices, const CreatureMotion &motion,
	float metersToModel)
	: CreatureDriver(motion.blend), motion_(motion), toModel_(metersToModel)
{
	body_ = skeleton.findBone("body");
	// 手足: <name>_left_hip / <name>_right_hip から、同じ頭の knee / ankle / foot を探す
	struct Found
	{
		Leg leg;
		std::string pair;
		int side = 0; // 0=左、1=右
	};
	std::vector<Found> found;
	for(size_t i = 0; i < skeleton.boneCount(); ++i){
		const std::string &name = skeleton.bone(static_cast<int>(i)).name;
		for(const int side : {0, 1}){
			const std::string suffix = side == 0 ? "_left_hip" : "_right_hip";
			if(name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0){
				continue;
			}
			const std::string prefix = name.substr(0, name.size() - 3);
			Found f;
			f.leg.hip = static_cast<int>(i);
			f.leg.knee = skeleton.findBone(prefix + "knee");
			f.leg.ankle = skeleton.findBone(prefix + "ankle");
			f.leg.foot = skeleton.findBone(prefix + "foot");
			f.pair = name.substr(0, name.size() - suffix.size());
			f.side = side;
			if(f.leg.knee < 0 || f.leg.ankle < 0 || f.leg.foot < 0 || found.size() >= kMaxLegs){
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "creature: leg '%s' is incomplete or too many legs (ignored)", name.c_str());
				continue;
			}
			found.push_back(std::move(f));
		}
	}
	// 1対ずつ、付け根の前後の位置で、前(-Z)から順に番号を付ける
	std::vector<std::pair<float, std::string>> pairs;
	for(const auto &f : found){
		const float z = restPosition(skeleton, f.leg.hip).z;
		const auto it = std::find_if(pairs.begin(), pairs.end(), [&](const auto &p){ return p.second == f.pair; });
		if(it == pairs.end()){
			pairs.emplace_back(z, f.pair);
		}
		else{
			it->first = std::min(it->first, z);
		}
	}
	std::sort(pairs.begin(), pairs.end());
	model::Vec3 rearSum;
	int rearCount = 0;
	for(auto &f : found){
		const size_t pairIndex = static_cast<size_t>(std::find_if(pairs.begin(), pairs.end(), [&](const auto &p){ return p.second == f.pair; }) - pairs.begin());
		Leg &leg = f.leg;
		const model::Vec3 hip = restPosition(skeleton, leg.hip), knee = restPosition(skeleton, leg.knee);
		const model::Vec3 ankle = restPosition(skeleton, leg.ankle), foot = restPosition(skeleton, leg.foot);
		const model::Vec3 reach = foot - hip;
		// 付け根を振る軸: 足先が前へ動く向き(reach × 前)。持ち上げる軸: 足先が上へ動く向き(reach × 上)。
		// ひざ・足首の軸: 前後の骨のなす面に垂直で、+の回転が、さらに折り曲げる向き
		leg.swingAxis = normalize(cross(reach, kForward), kAxisX);
		const model::Vec3 horizontal = cross(reach, kUp);
		leg.reach = std::max(length(horizontal), 0.25f * length(reach) + 1e-4f); // 真下へ伸びた足は、持ち上げで足先が上がりにくい
		leg.liftAxis = normalize(horizontal, leg.swingAxis);
		leg.kneeAxis = normalize(cross(knee - hip, ankle - knee), leg.swingAxis);
		leg.ankleAxis = normalize(cross(ankle - knee, foot - ankle), leg.kneeAxis);
		// 対角の足が同時に出る: 前から数えた対の番号と左右で、位相を半周期ずらす(3対なら、三脚歩行になる)
		leg.walkOffset = static_cast<float>((pairIndex + static_cast<size_t>(f.side)) % 2) * 0.5f;
		leg.front = pairIndex == 0;
		leg.rear = pairs.size() > 1 && pairIndex + 1 == pairs.size();
		leg.side = f.side;
		if(leg.rear){
			rearSum = rearSum + hip;
			++rearCount;
		}
		legs_.push_back(leg);
	}
	if(body_ < 0 || legs_.empty()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "creature: no body bone or no legs (body %d, legs %zu)", body_, legs_.size());
		return;
	}
	bodyRest_ = restPosition(skeleton, body_);
	rearPivot_ = rearCount > 0 ? rearSum * (1.0f / static_cast<float>(rearCount)) : bodyRest_;
	// 胴体のボーンだけに付く頂点(胴体・顔)を、間引いて覚える
	std::vector<model::Vec3> points;
	for(const auto &vertex : vertices){
		if(vertex.bones[0] == body_ && vertex.bones[1] < 0){
			points.push_back({vertex.position[0], vertex.position[1], vertex.position[2]});
		}
	}
	const size_t stride = std::max<size_t>(1, (points.size() + kMaxHullPoints - 1) / kMaxHullPoints);
	for(size_t i = 0; i < points.size(); i += stride){
		hull_.push_back(points[i]);
	}
}

const CreatureMotion::Walk *CreatureAnimator::walkOf(Motion motion) const
{
	switch(motion){
	case Motion::WalkFast: return &motion_.walkFast;
	case Motion::WalkSlow: return &motion_.walkSlow;
	default: return nullptr;
	}
}

float CreatureAnimator::stride(Motion motion) const
{
	const CreatureMotion::Walk *walk = walkOf(motion);
	return walk ? std::max(walk->stride, 0.05f) : 0.0f;
}

float CreatureAnimator::duration(Motion motion) const
{
	const auto &a = motion_.stand;
	const auto &j = motion_.jump;
	switch(motion){
	case Motion::AttackStand: return a.rise + a.wiggle + a.fall + a.hold + a.recover;
	case Motion::AttackJump: return j.crouch + j.launch + j.air + j.hold + j.recover;
	default: return 0.0f;
	}
}

CreatureDriver::JumpPhases CreatureAnimator::jumpPhases() const
{
	const auto &j = motion_.jump;
	return {j.crouch, j.crouch + j.launch, j.crouch + j.launch + j.air};
}

CreatureAnimator::StandPhase CreatureAnimator::standPhase(float t) const
{
	const auto &a = motion_.stand;
	StandPhase phase;
	const float fallStart = a.rise + a.wiggle;
	phase.fallEnd = fallStart + a.fall;
	phase.rise = smoothstep(0.0f, 1.0f, progress(t, 0.0f, a.rise));
	phase.fall = std::pow(progress(t, fallStart, a.fall), std::max(a.fallEase, 1.0f));
	phase.back = 1.0f - smoothstep(0.0f, 1.0f, progress(t, phase.fallEnd + a.hold, a.recover));
	return phase;
}

float CreatureAnimator::fallen(const State &state) const
{
	if(state.motion != Motion::AttackStand){
		return 0.0f;
	}
	const StandPhase phase = standPhase(state.actionTime);
	return phase.fall * phase.back;
}

// 潰れの量 c(+で縦に潰れる)から、高さを 1-c 倍、前後・左右を 1+stretch c 倍にする
CreatureAnimator::BodyScale CreatureAnimator::bodyScale(const State &state) const
{
	const CreatureMotion::Squash *squash = nullptr;
	float c = 0.0f;
	if(state.motion == Motion::AttackStand){
		squash = &motion_.stand.squash;
		c = springSquash(*squash, state.actionTime - standPhase(state.actionTime).fallEnd);
	}
	else if(state.motion == Motion::AttackJump){
		const auto &j = motion_.jump;
		squash = &j.squash;
		const float landTime = j.crouch + j.launch + j.air;
		if(state.actionTime >= landTime){
			c = springSquash(*squash, state.actionTime - landTime);
		}
		else if(state.actionTime >= j.crouch){
			// 跳んでいる間の伸び: 地面を蹴る間に伸び、跳び立ち・着地の前ほど大きく、いちばん高い所で4割
			const float launch = smoothstep(0.0f, 1.0f, progress(state.actionTime, j.crouch, j.launch));
			const float p = jumpProgress(state);
			c = -j.airStretch * launch * (0.4f + 0.6f * (1.0f - 2.0f * p) * (1.0f - 2.0f * p));
		}
	}
	if(!squash){
		return {};
	}
	return {1.0f - c, 1.0f + squash->stretch * c};
}

// 歩き: 足ごとに、位相 u が duty までは接地して、前から後ろへ一定の速さで送る。残りは浮かせて、後ろから前へ戻す(持ち上げ・ひざを曲げる)。
// 胴体は1歩ごとに上下に弾み(hop で、地面で跳ね返る形に)、前後に傾く
void CreatureAnimator::walkPose(const CreatureMotion::Walk &w, const State &state, Pose &pose) const
{
	const float duty = std::clamp(w.duty, 0.1f, 0.9f);
	for(size_t i = 0; i < legs_.size(); ++i){
		LegPose &leg = pose.legs[i];
		const float u = fract(state.walkPhase + legs_[i].walkOffset);
		if(u < duty){
			leg.swing = w.swing * (1.0f - 2.0f * u / duty);
			leg.planted = 1.0f;
		}
		else{
			const float t = (u - duty) / (1.0f - duty);
			const float up = std::sin(kPi * t);
			leg.swing = -w.swing * std::cos(kPi * t);
			leg.lift = w.lift * up;
			leg.knee = w.kneeLift * up;
			leg.ankle = -0.5f * w.kneeLift * up;
			leg.planted = 0.0f;
		}
	}
	const float p = state.walkPhase * kTwoPi;
	// 1周期に2歩(対角の組が1回ずつ)。滑らかな波 cos(2p) と、跳ね返る形 2|cos p|-1 を hop で混ぜる
	pose.bob = w.bob * lerp(std::cos(2.0f * p), 2.0f * std::fabs(std::cos(p)) - 1.0f, std::clamp(w.hop, 0.0f, 1.0f));
	pose.pitch = w.nod * std::sin(2.0f * p);
	pose.sway = w.sway * std::sin(p);
	pose.roll = w.roll * std::sin(p);
}

// 待機: 胴体がゆっくり上下・前後に傾き、前足が少し動く
void CreatureAnimator::idlePose(const State &state, Pose &pose) const
{
	const auto &idle = motion_.idle;
	const float p = state.clock * kTwoPi / std::max(idle.period, 0.1f);
	pose.bob = idle.breath * std::sin(p);
	pose.pitch = idle.pitch * std::sin(p);
	for(size_t i = 0; i < legs_.size(); ++i){
		if(legs_[i].front){
			pose.legs[i].swing = idle.frontSwing * std::sin(0.5f * p + 2.0f * static_cast<float>(legs_[i].side)); // 左右で少しずらす
		}
	}
}

// 攻撃A: 後ろ足の付け根を支点に、胴体を angle まで起こし(rise)、前足をわきわき動かし(wiggle)、前足を前へ伸ばしながら
// 加速して前へ倒れ込み(fall。顔から地面へ)、少し跳ね返って止まり(hold)、元の姿勢へ戻る(recover)。後ろ足は、胴体の傾きを打ち消して地面に残す
void CreatureAnimator::standPose(const State &state, Pose &pose) const
{
	const auto &a = motion_.stand;
	const float t = state.actionTime;
	const StandPhase phase = standPhase(t);
	const float rise = phase.rise, fall = phase.fall, back = phase.back;
	const float rebound = t >= phase.fallEnd ? a.bounce * std::sin(kPi * progress(t, phase.fallEnd, 0.2f)) : 0.0f; // 倒れた直後の跳ね返り
	pose.pivot = 1.0f;
	pose.pitch = (lerp(a.angle * rise, a.fallAngle, fall) + rebound) * back;
	const float w = progress(t, a.rise, a.wiggle);
	const float envelope = std::sin(kPi * w); // わきわきの振れ幅(始めと終わりは0)
	for(size_t i = 0; i < legs_.size(); ++i){
		LegPose &leg = pose.legs[i];
		if(!legs_[i].front){
			leg.level = 1.0f;
			continue;
		}
		// わきわき: 左右で逆向きに、付け根を振り、ひざを曲げ伸ばしする
		const float wiggle = envelope * std::sin(kTwoPi * a.wiggleCount * w + kPi * static_cast<float>(legs_[i].side));
		leg.raise = lerp(a.frontRaise * rise, a.reach, fall) * back;
		leg.knee = (lerp(a.frontFold * rise, a.reachKnee, fall) + a.wiggleKnee * wiggle) * back;
		leg.swing = a.wiggleSwing * wiggle;
		leg.open = a.reachOpen * fall * back;
		leg.planted = 1.0f - back;
	}
}

// 攻撃B: 溜め(crouch。胴体を沈め、ひざを曲げる) → 伸び上がる(launch) → 跳ぶ(air。足を広げ始め、胴体の傾きを戻す) →
// お腹から着地(手足を投げ出したまま、胴体が揺れる。hold) → 戻る(recover)。位置(放物線)は EnemyHorde が jumpProgress で動かす
void CreatureAnimator::jumpPose(const State &state, Pose &pose) const
{
	constexpr float kExtendKnee = -0.2f; // 伸び上がったときの、ひざの角度(伸ばす)
	constexpr float kFlop = 0.2f;        // 着地で、手足がさらに跳ね上がる割合
	constexpr float kFlopTime = 0.2f;
	const auto &j = motion_.jump;
	const float t = state.actionTime;
	const float launchStart = j.crouch, airStart = launchStart + j.launch, landTime = airStart + j.air, holdEnd = landTime + j.hold;
	const float crouch = smoothstep(0.0f, 1.0f, progress(t, 0.0f, j.crouch));
	const float launch = smoothstep(0.0f, 1.0f, progress(t, launchStart, j.launch));
	const float air = smoothstep(0.0f, 1.0f, progress(t, airStart, j.air));
	const float back = 1.0f - smoothstep(0.0f, 1.0f, progress(t, holdEnd, j.recover));
	const float impact = t - landTime;
	const bool landed = impact >= 0.0f;
	const float sink = crouch * (1.0f - launch);
	pose.bob = -j.crouchDepth * sink;
	pose.pitch = j.crouchPitch * sink + j.airPitch * launch * (1.0f - air);
	if(landed){
		pose.pitch += j.bounce * std::sin(kTwoPi * impact / 0.3f) * std::exp(-6.0f * impact) * back; // 着地の衝撃で揺れる
	}
	const float flop = landed ? kFlop * std::sin(kPi * progress(impact, 0.0f, kFlopTime)) : 0.0f;
	for(size_t i = 0; i < legs_.size(); ++i){
		LegPose &leg = pose.legs[i];
		leg.knee = j.crouchKnee * sink + kExtendKnee * launch * (1.0f - air);
		leg.lift = j.spread * (air + flop) * back;
		leg.swing = (legs_[i].front ? 1.0f : -1.0f) * j.spreadSwing * air * back;
		leg.planted = t < airStart ? 1.0f - launch : 1.0f - back;
	}
}

// 倒れ: 手足を縮めて、横へ倒れる(どの足も接地していない扱い)
void CreatureAnimator::deathPose(const State &state, Pose &pose) const
{
	const auto &d = motion_.death;
	const float e = smoothstep(0.0f, std::max(d.duration, 0.05f), state.actionTime);
	for(size_t i = 0; i < legs_.size(); ++i){
		pose.legs[i].knee = d.curl * e;
		pose.legs[i].lift = 0.6f * d.curl * e;
		pose.legs[i].planted = 0.0f;
	}
	pose.roll = d.roll * e;
}

CreatureAnimator::Pose CreatureAnimator::pose(Motion motion, const State &state) const
{
	Pose result;
	switch(motion){
	case Motion::Idle: idlePose(state, result); break;
	case Motion::WalkFast: walkPose(motion_.walkFast, state, result); break;
	case Motion::WalkSlow: walkPose(motion_.walkSlow, state, result); break;
	case Motion::AttackStand: standPose(state, result); break;
	case Motion::AttackJump: jumpPose(state, result); break;
	case Motion::Death: deathPose(state, result); break;
	}
	return result;
}

// 手足の回転を当てる。接地している足は、drop[i](モデルの単位)だけ足先を下げる(付け根で持ち上げる角度を、水平の距離で割った分だけ減らす。小さい角度の近似)。
// level の分だけ、胴体の前後の傾きを付け根で打ち消す
void CreatureAnimator::setLegs(model::Skeleton &skeleton, const Pose &pose, const std::array<float, kMaxLegs> &drop) const
{
	for(size_t i = 0; i < legs_.size(); ++i){
		const Leg &leg = legs_[i];
		const LegPose &p = pose.legs[i];
		const float lift = p.lift - p.planted * drop[i] / leg.reach;
		const float outward = leg.side == 0 ? -1.0f : 1.0f; // Y軸まわりで外へ開く向き(左の手足は+X側)
		skeleton.setBoneRotation(leg.hip, model::Quat::fromAxisAngle(kAxisX, -p.level * pose.pitch)
			* model::Quat::fromAxisAngle(leg.liftAxis, lift) * model::Quat::fromAxisAngle(leg.swingAxis, p.swing)
			* model::Quat::fromAxisAngle(kUp, outward * p.open) * model::Quat::fromAxisAngle(kAxisX, p.raise));
		skeleton.setBoneRotation(leg.knee, model::Quat::fromAxisAngle(leg.kneeAxis, p.knee));
		skeleton.setBoneRotation(leg.ankle, model::Quat::fromAxisAngle(leg.ankleAxis, p.ankle));
	}
}

void CreatureAnimator::apply(model::Skeleton &skeleton, const State &state) const
{
	skeleton.resetPose();
	if(body_ < 0){
		return;
	}
	// 前の動きと混ぜる(値ごとに直線で)
	Pose p = pose(state.motion, state);
	if(state.fade < 1.0f){
		const Pose from = pose(state.previous, state);
		const float t = smoothstep(0.0f, 1.0f, state.fade);
		for(size_t i = 0; i < legs_.size(); ++i){
			LegPose &to = p.legs[i];
			const LegPose &f = from.legs[i];
			to = {lerp(f.swing, to.swing, t), lerp(f.lift, to.lift, t), lerp(f.knee, to.knee, t), lerp(f.ankle, to.ankle, t),
				lerp(f.raise, to.raise, t), lerp(f.open, to.open, t), lerp(f.planted, to.planted, t), lerp(f.level, to.level, t)};
		}
		p.pitch = lerp(from.pitch, p.pitch, t);
		p.roll = lerp(from.roll, p.roll, t);
		p.pivot = lerp(from.pivot, p.pivot, t);
		p.bob = lerp(from.bob, p.bob, t);
		p.sway = lerp(from.sway, p.sway, t);
		p.lunge = lerp(from.lunge, p.lunge, t);
	}
	// 胴体の傾き(+pitch で前が上がる = X軸まわりの+)、横の傾き(Z軸まわり)。前後の傾きの支点を、pivot の分だけ後ろ足の付け根へ寄せる
	// (胴体のボーンの位置から支点への腕 arm が、回したあとも支点に戻るよう、arm - R arm だけ動かす)
	const model::Quat rotation = model::Quat::fromAxisAngle(kAxisX, p.pitch) * model::Quat::fromAxisAngle(kAxisZ, p.roll);
	const model::Vec3 arm = (rearPivot_ - bodyRest_) * p.pivot;
	const model::Vec3 offset = arm - rotation.rotate(arm) + model::Vec3{p.sway * toModel_, 0.0f, -p.lunge * toModel_};
	skeleton.setBoneRotation(body_, rotation);
	skeleton.setBoneTranslation(body_, offset);
	// いったん上下に動かさずに解いて、足先と胴体のいちばん低い所を求める(浮いている足でも、地面より下へ行くなら、その足で支える)
	std::array<float, kMaxLegs> footY{};
	setLegs(skeleton, p, footY);
	skeleton.update();
	float lowest = 0.0f;
	for(size_t i = 0; i < legs_.size(); ++i){
		footY[i] = skeleton.globalPosition(legs_[i].foot).y;
		lowest = i == 0 ? footY[i] : std::min(lowest, footY[i]);
	}
	const model::Mat34 &m = skeleton.skinMatrices()[static_cast<size_t>(body_)];
	float bodyLowest = std::numeric_limits<float>::max();
	for(const auto &v : hull_){
		bodyLowest = std::min(bodyLowest, m.r[1][0] * v.x + m.r[1][1] * v.y + m.r[1][2] * v.z + m.t[1]);
	}
	// 胴体を、いちばん低い所が地面に着く高さ+上下の揺れ(bob)だけ動かす(揺れで沈めても、胴体は地面より下へ行かない)。
	// 接地している足は、浮く分(胴体の傾き・揺れなど)だけ下げて、足先を地面に残す
	const float lift = std::max(p.bob * toModel_ - std::min(lowest, bodyLowest), -bodyLowest);
	for(size_t i = 0; i < legs_.size(); ++i){
		footY[i] += lift;
	}
	setLegs(skeleton, p, footY);
	skeleton.setBoneTranslation(body_, offset + model::Vec3{0.0f, lift, 0.0f});
}

} // namespace game
