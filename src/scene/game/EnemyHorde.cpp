#include "scene/game/EnemyHorde.h"
#include "field/FieldMap.h"
#include "field/FieldMovement.h"
#include "field/PropCollision.h"
#include "model/AnimationClip.h"
#include "resources/LuaTable.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "scene/common/BlobShadow.h"
#include "scene/common/ModelFactory.h"
#include "sdl/SDLVulkanWindow.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <sol/sol.hpp>

namespace game
{

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kRadius = 0.25f;        // 地面の歩けない所・フィールドの縁との間に空ける距離
constexpr float kStopHysteresis = 0.5f; // 止まった後、この分だけ離れたら、また歩き出す(境目で歩く・止まるを繰り返さない)
constexpr float kStillWeight = 1e-3f;   // 歩きの振れ幅がこれ以下なら、止まりきった(姿勢が変わらない)とみなす
constexpr int kOverlapIterations = 6;   // 箱の重なりを解く、くり返しの回数(1フレームに)
// 重なりを押し出すときの、動きやすさ(2体の比で押し出す量を分ける): 歩いている個体は、割り込む側として大きく押し戻され、
// 止まっている(待機)・攻撃している個体は、その場に残りやすい(後ろから来た個体が、前の個体を押し込まずに、後ろへ並ぶ)
constexpr float kWalkMobility = 1.0f;
constexpr float kIdleMobility = 0.3f;
constexpr float kAttackMobility = 0.1f;
constexpr float kPinnedMobility = 0.01f; // 前のフレームで、押し出しを地形に止められた個体(壁際・崖際。相手の方が動く)
constexpr float kPinnedSlip = 0.005f;    // 押し出した先と、地形に沿って動けた位置が、これより離れたら、地形に止められたとみなす(m)

// 角度aをtargetへ、最短の向きで最大maxStepだけ近づける
float approachAngle(float a, float target, float maxStep)
{
	float diff = std::fmod(target - a + kPi, kTwoPi);
	if(diff < 0.0f){
		diff += kTwoPi;
	}
	diff -= kPi;
	return a + std::clamp(diff, -maxStep, maxStep);
}

// 攻撃B の動きの時刻 time を、実時間 dt だけ進めた時刻。空中の区間(airStart〜airEnd)は airRate(動きの秒 ÷ 実時間の秒)、ほかは rate で進む
float advanceJumpTime(float time, float dt, float rate, float airStart, float airEnd, float airRate)
{
	while(dt > 0.0f){
		const bool inAir = time >= airStart && time < airEnd;
		const float speed = std::max(inAir ? airRate : rate, 1e-3f);
		const float next = time < airStart ? airStart : (inAir ? airEnd : std::numeric_limits<float>::max());
		const float need = (next - time) / speed; // 次の区間の境までの実時間
		if(need >= dt){
			return time + dt * speed;
		}
		time = next;
		dt -= need;
	}
	return time;
}

model::Quat rotationX(float radians) { return model::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, radians); }
model::Quat rotationY(float radians) { return model::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians); }
model::Quat rotationZ(float radians) { return model::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, radians); }

void readFloat(const sol::table &table, const char *key, float &value)
{
	value = table.get_or(key, value);
}

void readFloat(const sol::optional<sol::table> &table, const char *key, float &value)
{
	if(table){
		readFloat(*table, key, value);
	}
}

void readInt(const sol::optional<sol::table> &table, const char *key, int &value)
{
	if(table){
		value = table->get_or(key, value);
	}
}

void readString(const sol::optional<sol::table> &table, const char *key, std::string &value)
{
	if(table){
		value = table->get_or(key, value);
	}
}

// table[key] の配列({a, b, c} など)から、先頭の N 個を読む(無い要素は、そのまま)
template<size_t N>
void readArray(const sol::optional<sol::table> &table, const char *key, float (&values)[N])
{
	if(!table){
		return;
	}
	const sol::optional<sol::table> list = (*table)[key];
	if(!list){
		return;
	}
	for(size_t i = 0; i < N; ++i){
		values[i] = list->get_or(static_cast<int>(i + 1), values[i]);
	}
}

// 丸い生き物の体の形(enemies.lua の creature の表)
model::CreatureSpec readCreatureSpec(const sol::table &t)
{
	model::CreatureSpec spec;
	const sol::optional<sol::table> body = t["body"], face = t["face"], legMesh = t["legMesh"], color = t["color"];
	readFloat(body, "length", spec.body.length);
	readFloat(body, "width", spec.body.width);
	readFloat(body, "height", spec.body.height);
	readFloat(body, "taper", spec.body.taper);
	readInt(body, "slices", spec.body.slices);
	readInt(body, "sides", spec.body.sides);
	readFloat(face, "pitch", spec.face.pitch);
	readFloat(face, "lift", spec.face.lift);
	readFloat(face, "eyeSpacing", spec.face.eyeSpacing);
	readFloat(face, "eyeUp", spec.face.eyeUp);
	readFloat(face, "eyeRadius", spec.face.eyeRadius);
	readFloat(face, "eyeSpan", spec.face.eyeSpan);
	readFloat(face, "eyeThickness", spec.face.eyeThickness);
	readInt(face, "eyeSegments", spec.face.eyeSegments);
	readFloat(face, "mouthUp", spec.face.mouthUp);
	readFloat(face, "mouthWidth", spec.face.mouthWidth);
	readFloat(face, "mouthHeight", spec.face.mouthHeight);
	readInt(face, "mouthSides", spec.face.mouthSides);
	readInt(legMesh, "sides", spec.legMesh.sides);
	readInt(legMesh, "rings", spec.legMesh.rings);
	readFloat(legMesh, "blend", spec.legMesh.blend);
	readFloat(legMesh, "inset", spec.legMesh.inset);
	readArray(color, "back", spec.color.back);
	readArray(color, "belly", spec.color.belly);
	readArray(color, "legs", spec.color.legs);
	readArray(color, "face", spec.color.face);
	readFloat(t, "bellyLine", spec.bellyLine);
	if(const sol::optional<sol::table> legs = t["legs"]){
		for(size_t i = 1; i <= legs->size(); ++i){
			const sol::optional<sol::table> entry = (*legs)[i];
			if(!entry){
				continue;
			}
			model::CreatureSpec::Leg leg;
			readString(entry, "name", leg.name);
			readFloat(entry, "along", leg.along);
			readFloat(entry, "angle", leg.angle);
			readArray(entry, "knee", leg.knee);
			readArray(entry, "ankle", leg.ankle);
			readArray(entry, "foot", leg.foot);
			readArray(entry, "radius", leg.radius);
			spec.legs.push_back(std::move(leg));
		}
	}
	return spec;
}

// 潰れ(attackStand / attackJump の squash の表)
void readSquash(const sol::optional<sol::table> &motion, CreatureMotion::Squash &squash)
{
	if(!motion){
		return;
	}
	const sol::optional<sol::table> t = (*motion)["squash"];
	readFloat(t, "amount", squash.amount);
	readFloat(t, "stretch", squash.stretch);
	readFloat(t, "duration", squash.duration);
	readFloat(t, "bounce", squash.bounce);
}

void readWalk(const sol::optional<sol::table> &walk, CreatureMotion::Walk &w)
{
	readFloat(walk, "speed", w.speed);
	readFloat(walk, "stride", w.stride);
	readFloat(walk, "duty", w.duty);
	readFloat(walk, "swing", w.swing);
	readFloat(walk, "lift", w.lift);
	readFloat(walk, "kneeLift", w.kneeLift);
	readFloat(walk, "bob", w.bob);
	readFloat(walk, "hop", w.hop);
	readFloat(walk, "nod", w.nod);
	readFloat(walk, "sway", w.sway);
	readFloat(walk, "roll", w.roll);
}

// 丸い生き物の体の動きと行動(enemies.lua の walkFast / walkSlow / idle / attackStand / attackJump / death / behavior の表)
void readCreatureMotion(const sol::table &t, EnemyType &type)
{
	CreatureMotion &motion = type.creatureMotion;
	const sol::optional<sol::table> idle = t["idle"], stand = t["attackStand"], jump = t["attackJump"], death = t["death"], behavior = t["behavior"];
	readWalk(t["walkFast"], motion.walkFast);
	readWalk(t["walkSlow"], motion.walkSlow);
	readFloat(t, "motionBlend", motion.blend);
	readFloat(idle, "period", motion.idle.period);
	readFloat(idle, "breath", motion.idle.breath);
	readFloat(idle, "pitch", motion.idle.pitch);
	readFloat(idle, "frontSwing", motion.idle.frontSwing);
	auto &a = motion.stand;
	readFloat(stand, "rise", a.rise);
	readFloat(stand, "angle", a.angle);
	readFloat(stand, "frontRaise", a.frontRaise);
	readFloat(stand, "frontFold", a.frontFold);
	readFloat(stand, "wiggle", a.wiggle);
	readFloat(stand, "wiggleCount", a.wiggleCount);
	readFloat(stand, "wiggleSwing", a.wiggleSwing);
	readFloat(stand, "wiggleKnee", a.wiggleKnee);
	readFloat(stand, "fall", a.fall);
	readFloat(stand, "fallEase", a.fallEase);
	readFloat(stand, "fallAngle", a.fallAngle);
	readFloat(stand, "reach", a.reach);
	readFloat(stand, "reachOpen", a.reachOpen);
	readFloat(stand, "reachKnee", a.reachKnee);
	readFloat(stand, "bounce", a.bounce);
	readFloat(stand, "hold", a.hold);
	readFloat(stand, "recover", a.recover);
	readSquash(stand, a.squash);
	auto &j = motion.jump;
	readFloat(jump, "crouch", j.crouch);
	readFloat(jump, "crouchDepth", j.crouchDepth);
	readFloat(jump, "crouchKnee", j.crouchKnee);
	readFloat(jump, "crouchPitch", j.crouchPitch);
	readFloat(jump, "launch", j.launch);
	readFloat(jump, "air", j.air);
	readFloat(jump, "height", j.height);
	readFloat(jump, "distance", j.distance);
	readFloat(jump, "landGap", j.landGap);
	readFloat(jump, "airPitch", j.airPitch);
	readFloat(jump, "spread", j.spread);
	readFloat(jump, "spreadSwing", j.spreadSwing);
	readFloat(jump, "bounce", j.bounce);
	readFloat(jump, "hold", j.hold);
	readFloat(jump, "recover", j.recover);
	readFloat(jump, "airStretch", j.airStretch);
	readFloat(jump, "landGapJitter", j.landGapJitter);
	readFloat(jump, "landAngleJitter", j.landAngleJitter);
	readInt(jump, "landTries", j.landTries);
	readFloat(jump, "minDistance", j.minDistance);
	readFloat(jump, "minHeight", j.minHeight);
	readFloat(jump, "maxHeight", j.maxHeight);
	readFloat(jump, "gravity", j.gravity);
	readFloat(jump, "clearance", j.clearance);
	readFloat(jump, "probeStep", j.probeStep);
	readSquash(jump, j.squash);
	readFloat(death, "duration", motion.death.duration);
	readFloat(death, "roll", motion.death.roll);
	readFloat(death, "curl", motion.death.curl);
	auto &b = type.behavior;
	readFloat(behavior, "fastRatio", b.fastRatio);
	readFloat(behavior, "closeRange", b.closeRange);
	readFloat(behavior, "jumpMin", b.jumpMin);
	readFloat(behavior, "jumpRange", b.jumpRange);
	readFloat(behavior, "standChance", b.standChance);
	readFloat(behavior, "jumpChance", b.jumpChance);
	readFloat(behavior, "cooldown", b.cooldown);
	readFloat(behavior, "cooldownJitter", b.cooldownJitter);
	readFloat(behavior, "retry", b.retry);
	readFloat(behavior, "timeJitter", b.timeJitter);
}

void readClipEntry(const sol::optional<sol::table> &motions, const char *key, CreatureClipMotion::Entry &entry)
{
	if(!motions){
		return;
	}
	const sol::optional<sol::table> t = (*motions)[key];
	readString(t, "clip", entry.clip);
	readFloat(t, "stride", entry.stride);
	if(t){
		entry.loop = t->get_or("loop", entry.loop);
	}
}

// クリップで動かすときの表(enemies.lua の clips・motions・rootMotionBones)
void readClipMotion(const sol::table &t, CreatureClipMotion &clips)
{
	clips.path = t.get_or("clips", clips.path);
	const sol::optional<sol::table> motions = t["motions"];
	readClipEntry(motions, "idle", clips.idle);
	readClipEntry(motions, "walkFast", clips.walkFast);
	readClipEntry(motions, "walkSlow", clips.walkSlow);
	readClipEntry(motions, "attackStand", clips.attackStand);
	readClipEntry(motions, "attackJump", clips.attackJump);
	readClipEntry(motions, "death", clips.death);
	sol::optional<sol::table> stand, jump;
	if(motions){
		stand = (*motions)["attackStand"].get<sol::optional<sol::table>>();
		jump = (*motions)["attackJump"].get<sol::optional<sol::table>>();
	}
	readFloat(stand, "fallStart", clips.stand.fallStart);
	readFloat(stand, "fallEnd", clips.stand.fallEnd);
	readFloat(stand, "recoverStart", clips.stand.recoverStart);
	readFloat(stand, "recoverEnd", clips.stand.recoverEnd);
	readFloat(jump, "crouchEnd", clips.jump.crouchEnd);
	readFloat(jump, "launchEnd", clips.jump.launchEnd);
	readFloat(jump, "airEnd", clips.jump.airEnd);
	if(const sol::optional<sol::table> bones = t["rootMotionBones"]){
		for(size_t i = 1; i <= bones->size(); ++i){
			clips.rootMotionBones.push_back(bones->get_or(static_cast<int>(i), std::string()));
		}
	}
}

int findBone(const model::Skeleton &skeleton, const std::string &name, const std::string &kindName)
{
	const int index = skeleton.findBone(name);
	if(index < 0){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy '%s': no bone '%s' (not animated)", kindName.c_str(), name.c_str());
	}
	return index;
}

void rotate(model::Skeleton &skeleton, int bone, const model::Quat &rotation)
{
	if(bone >= 0){
		skeleton.setBoneRotation(bone, rotation);
	}
}
}

std::vector<EnemyType> loadEnemyTypes(const std::string &relativePath)
{
	std::vector<EnemyType> result;
	sol::state lua;
	const sol::optional<sol::table> enemies = loadLuaTable(lua, relativePath, "enemies", "enemy table");
	if(!enemies){
		return result;
	}
	for(size_t i = 1; i <= enemies->size(); ++i){
		const sol::optional<sol::table> entry = (*enemies)[i];
		if(!entry){
			continue;
		}
		const sol::table &t = *entry;
		EnemyType type;
		type.name = t.get_or("name", std::string("enemy"));
		if(const sol::optional<sol::table> creature = t["creature"]){
			type.creature = readCreatureSpec(*creature);
		}
		type.model = t.get_or("model", std::string());
		type.count = std::max(t.get_or("count", 0), 0);
		type.seed = t.get_or("seed", type.seed);
		readFloat(t, "height", type.height);
		readFloat(t, "length", type.length);
		readFloat(t, "speed", type.speed);
		readFloat(t, "speedJitter", type.speedJitter);
		readFloat(t, "turnSpeed", type.turnSpeed);
		readFloat(t, "spawnMinRadius", type.spawnMinRadius);
		readFloat(t, "spawnRadius", type.spawnRadius);
		readFloat(t, "stopDistance", type.stopDistance);
		readFloat(t, "stopJitter", type.stopJitter);
		readFloat(t, "cameraCullRadius", type.cameraCullRadius);
		readFloat(t, "shadowRadius", type.shadowRadius);
		readFloat(t, "shadowOpacity", type.shadowOpacity);
		const sol::optional<sol::table> gait = t["gait"], lod = t["lod"], bones = t["bones"], box = t["box"];
		readFloat(box, "width", type.box.width);
		readFloat(box, "length", type.box.length);
		readFloat(box, "height", type.box.height);
		readFloat(box, "fallLength", type.box.fallLength);
		readFloat(lod, "near", type.lod.near);
		readFloat(lod, "far", type.lod.far);
		readFloat(lod, "interval", type.lod.interval);
		if(type.creature){
			readCreatureMotion(t, type);
			readClipMotion(t, type.clipMotion);
			result.push_back(std::move(type));
			continue;
		}
		// PMXの体: gait はMMDの名前のボーンの歩き
		readFloat(gait, "stride", type.gait.stride);
		readFloat(gait, "legSwing", type.gait.legSwing);
		readFloat(gait, "kneeBend", type.gait.kneeBend);
		readFloat(gait, "ankle", type.gait.ankle);
		readFloat(gait, "armSwing", type.gait.armSwing);
		readFloat(gait, "armDown", type.gait.armDown);
		readFloat(gait, "elbowBend", type.gait.elbowBend);
		readFloat(gait, "twist", type.gait.twist);
		readFloat(gait, "lean", type.gait.lean);
		readFloat(gait, "bob", type.gait.bob);
		readFloat(gait, "sway", type.gait.sway);
		readFloat(gait, "blend", type.gait.blend);
		readString(bones, "center", type.bones.center);
		readString(bones, "upper", type.bones.upper);
		readString(bones, "lower", type.bones.lower);
		readString(bones, "rightLeg", type.bones.rightLeg);
		readString(bones, "leftLeg", type.bones.leftLeg);
		readString(bones, "rightKnee", type.bones.rightKnee);
		readString(bones, "leftKnee", type.bones.leftKnee);
		readString(bones, "rightAnkle", type.bones.rightAnkle);
		readString(bones, "leftAnkle", type.bones.leftAnkle);
		readString(bones, "rightArm", type.bones.rightArm);
		readString(bones, "leftArm", type.bones.leftArm);
		readString(bones, "rightElbow", type.bones.rightElbow);
		readString(bones, "leftElbow", type.bones.leftElbow);
		if(type.model.empty()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy table: '%s' has no creature or model (%s)", type.name.c_str(), relativePath.c_str());
			continue;
		}
		type.gait.stride = std::max(type.gait.stride, 0.05f);
		result.push_back(std::move(type));
	}
	return result;
}

// 大きさ: 体長(前後の長さ)か、背の高さ(頂点の最大の高さ)に合わせる
EnemySize measureEnemy(const EnemyType &type, const std::vector<model::ModelVertex> &vertices)
{
	EnemySize size;
	float front = 0.0f, back = 0.0f;
	for(const auto &vertex : vertices){
		size.top = std::max(size.top, vertex.position[1]);
		front = std::min(front, vertex.position[2]);
		back = std::max(back, vertex.position[2]);
		size.side = std::max(size.side, std::fabs(vertex.position[0]));
	}
	size.length = back - front;
	size.scale = type.length > 0.0f && size.length > 0.0f ? type.length / size.length : (size.top > 0.0f ? type.height / size.top : 1.0f);
	return size;
}

// モデルから、大きさ・見えるかの判定の球・動かすボーンを求める。ボーンの無いモデルはfalse
bool EnemyHorde::setupKind(Kind &kind, VulkanModel &model)
{
	auto *skeleton = model.skeleton();
	const auto &name = kind.def.name;
	if(!skeleton){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy '%s': the model has no bones", name.c_str());
		return false;
	}
	const auto &vertices = model.data().vertices;
	const EnemySize size = measureEnemy(kind.def, vertices);
	const float top = size.top, modelLength = size.length, side = size.side;
	kind.scale = size.scale;
	// 当たりの箱: 表に無ければ、全部の頂点を囲む大きさ
	const auto &box = kind.def.box;
	kind.boxWidth = box.width > 0.0f ? box.width : 2.0f * side * kind.scale;
	kind.boxLength = box.length > 0.0f ? box.length : modelLength * kind.scale;
	kind.boxHeight = box.height > 0.0f ? box.height : top * kind.scale;
	// 見えるかの判定の球: 足元の真上、高さの半分を中心に、全部の頂点を囲む(手足を動かした分の余裕を足す)
	float radius2 = 0.0f;
	for(const auto &vertex : vertices){
		const float dy = vertex.position[1] - 0.5f * top;
		radius2 = std::max(radius2, vertex.position[0] * vertex.position[0] + dy * dy + vertex.position[2] * vertex.position[2]);
	}
	kind.boundY = 0.5f * top * kind.scale;
	kind.boundRadius = std::max(1.2f * std::sqrt(radius2) * kind.scale, kind.def.shadowRadius);
	kind.bodyRadius = 0.5f * std::min(kind.boxWidth, kind.boxLength);
	SDL_Log("enemy '%s': model height %.2f, length %.2f units -> scale %.4f (%.2f m tall, %.2f m long, %.2f m wide), %zu vertices, box %.2f x %.2f x %.2f m",
		name.c_str(), top, modelLength, kind.scale, top * kind.scale, modelLength * kind.scale, 2.0f * side * kind.scale, vertices.size(),
		kind.boxWidth, kind.boxLength, kind.boxHeight);
	if(kind.def.creature){
		// clips があれば、クリップの再生で動かす(読めなければ、数式で動かす)
		const CreatureMotion &motion = kind.def.creatureMotion;
		const CreatureClipMotion &clipMotion = kind.def.clipMotion;
		if(!clipMotion.path.empty()){
			auto clips = std::make_shared<const std::vector<model::AnimationClip>>(model::loadAnimationClips(ResourcePaths::resource(clipMotion.path.c_str())));
			if(!clips->empty()){
				kind.creature = std::make_unique<CreatureClipAnimator>(clips, *skeleton, clipMotion, motion.walkFast.speed, motion.walkSlow.speed, motion.blend,
					1.0f / kind.scale);
				SDL_Log("enemy '%s': creature moved by %zu clips (%s)", name.c_str(), clips->size(), clipMotion.path.c_str());
			}
		}
		if(!kind.creature){
			auto animator = std::make_unique<CreatureAnimator>(*skeleton, vertices, motion, 1.0f / kind.scale);
			SDL_Log("enemy '%s': creature with %zu legs (procedural motion)", name.c_str(), animator->legCount());
			kind.creature = std::move(animator);
		}
	}
	else{
		setupPmxBones(kind, *skeleton);
	}
	return true;
}

// PMXの歩きで動かす、MMDの名前のボーンの番号と、脚の長さ・腕の向き
void EnemyHorde::setupPmxBones(Kind &kind, model::Skeleton &skeleton)
{
	const auto &b = kind.def.bones;
	const auto &name = kind.def.name;
	kind.center = findBone(skeleton, b.center, name);
	kind.upper = findBone(skeleton, b.upper, name);
	kind.lower = findBone(skeleton, b.lower, name);
	kind.leg[0] = findBone(skeleton, b.rightLeg, name);
	kind.leg[1] = findBone(skeleton, b.leftLeg, name);
	kind.knee[0] = findBone(skeleton, b.rightKnee, name);
	kind.knee[1] = findBone(skeleton, b.leftKnee, name);
	kind.ankle[0] = findBone(skeleton, b.rightAnkle, name);
	kind.ankle[1] = findBone(skeleton, b.leftAnkle, name);
	kind.arm[0] = findBone(skeleton, b.rightArm, name);
	kind.arm[1] = findBone(skeleton, b.leftArm, name);
	kind.elbow[0] = findBone(skeleton, b.rightElbow, name);
	kind.elbow[1] = findBone(skeleton, b.leftElbow, name);
	skeleton.resetPose();
	skeleton.update();
	if(kind.leg[0] >= 0 && kind.ankle[0] >= 0){
		kind.legLength = skeleton.globalPosition(kind.leg[0]).y - skeleton.globalPosition(kind.ankle[0]).y;
	}
	// 腕を下ろす回転(Z軸まわり)の向き: 腕が体の -X 側へ伸びていれば +、+X 側なら -
	for(int side = 0; side < 2; ++side){
		if(kind.arm[side] >= 0 && kind.elbow[side] >= 0){
			kind.armSide[side] = skeleton.globalPosition(kind.elbow[side]).x < skeleton.globalPosition(kind.arm[side]).x ? 1.0f : -1.0f;
		}
	}
	SDL_Log("enemy '%s': leg %.2f units", name.c_str(), kind.legLength);
}

EnemyHorde::EnemyHorde(SDL_::VulkanWindow &window, ResourceSet &resources, const std::vector<EnemyType> &types, const field::FieldMap &map,
	float playerX, float playerZ)
{
	const float fieldW = map.width() * map.cellSize(), fieldD = map.depth() * map.cellSize();
	for(const auto &type : types){
		if(type.count <= 0){
			continue;
		}
		Kind kind;
		kind.def = type;
		std::mt19937 rng(type.seed);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const size_t kindIndex = kinds_.size();
		const size_t firstEnemy = enemies_.size();
		// 丸い生き物の体は、形の表からモデルデータを1つ作り、全部の個体で共有する(GPUのバッファは個体ごと)
		const std::shared_ptr<const model::ModelData> creatureData = type.creature ? model::buildCreature(*type.creature, type.name) : nullptr;
		for(int i = 0; i < type.count; ++i){
			auto model = creatureData
				? VulkanModel::create(window.getContext(), window.getBonePool(), window.getTexturePool(), creatureData,
					[](const std::string &){ return std::shared_ptr<VulkanTexture>(); }, SDL_::VulkanWindow::kFrameSlots)
				: createVulkanModel(window, resources, type.model);
			if(!model){
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy '%s': failed to create the model %s (%d of %d)", type.name.c_str(),
					creatureData ? "(creature)" : type.model.c_str(), i, type.count);
				break;
			}
			if(i == 0 && !setupKind(kind, *model)){
				break;
			}
			model->skeleton()->setIkEnabled(false); // 足IKが脚の振りを打ち消さないよう、FKだけで動かす
			model->setAmbientBoost(0.0f);
			Enemy enemy;
			enemy.kind = kindIndex;
			enemy.model = std::move(model);
			// プレイヤーのまわりの輪(spawnMinRadius〜spawnRadius)の中に、面積で一様にばらまく
			const float angle = unit(rng) * kTwoPi;
			const float r0 = std::max(type.spawnMinRadius, 0.0f), r1 = std::max(type.spawnRadius, r0);
			const float r = std::sqrt(r0 * r0 + (r1 * r1 - r0 * r0) * unit(rng));
			enemy.x = std::clamp(playerX + r * std::sin(angle), kRadius, std::max(kRadius, fieldW - kRadius));
			enemy.z = std::clamp(playerZ + r * std::cos(angle), kRadius, std::max(kRadius, fieldD - kRadius));
			enemy.y = map.heightAt(enemy.x, enemy.z);
			enemy.yaw = std::atan2(playerX - enemy.x, playerZ - enemy.z);
			enemy.phase = unit(rng);
			enemy.speedScale = 1.0f + type.speedJitter * (unit(rng) * 2.0f - 1.0f);
			enemy.stopDistance = type.stopDistance + type.stopJitter * unit(rng);
			enemy.poseTimer = type.lod.interval * unit(rng); // 間引くときの更新の時期を、個体ごとにずらす
			// 丸い生き物の体: 早歩きか、ゆっくり歩きか、待機・攻撃の速さ・時計・最初に攻撃を試すまでの時間の個体差(全員が揃って動かないように)
			const auto &behavior = type.behavior;
			enemy.fast = unit(rng) < behavior.fastRatio;
			enemy.creature.rate = 1.0f + behavior.timeJitter * (unit(rng) * 2.0f - 1.0f);
			enemy.creature.clock = 100.0f * unit(rng);
			enemy.creature.walkPhase = enemy.phase;
			enemy.cooldown = (behavior.cooldown + behavior.cooldownJitter) * unit(rng);
			enemies_.push_back(std::move(enemy));
		}
		if(enemies_.size() == firstEnemy){
			continue;
		}
		// 2体の箱が重なりうる、位置の間の距離の最大(箱の、位置からいちばん遠い角まで ×2。倒れ込みで前へ伸ばす分を含む)
		gridCell_ = std::max(gridCell_, 2.0f * std::hypot(0.5f * kind.boxWidth, 0.5f * kind.boxLength + std::max(type.box.fallLength, 0.0f)));
		kinds_.push_back(std::move(kind));
		SDL_Log("enemy '%s': %zu spawned", type.name.c_str(), enemies_.size() - firstEnemy);
	}
	for(auto &enemy : enemies_){
		applyPose(kinds_[enemy.kind], enemy); // 最初の姿勢(PMXの休止ポーズのAポーズのまま出さない)
	}
	// 箱の重なりを調べる格子: フィールド全体を、gridCell_ の大きさのセルに分ける(隣のセルまで調べれば、重なりうる相手が全部見つかる)
	gridW_ = std::max(1, static_cast<int>(std::ceil(fieldW / gridCell_)));
	gridD_ = std::max(1, static_cast<int>(std::ceil(fieldD / gridCell_)));
	cellStart_.resize(static_cast<size_t>(gridW_) * gridD_ + 1);
	cellItems_.resize(enemies_.size());
	cellOf_.resize(enemies_.size());
	colliders_.resize(enemies_.size());
	fromX_.resize(enemies_.size());
	fromZ_.resize(enemies_.size());
	for(size_t i = 0; i < enemies_.size(); ++i){
		colliders_[i] = colliderOf(enemies_[i]);
	}
	buildGrid();
}

EnemyHorde::~EnemyHorde() = default;

// 個体の当たりの箱: 向き(yaw)に合わせて回した箱(幅×長さ)を囲む、軸に平行な箱。攻撃Aで前へ倒れ込んでいる間は、倒れた度合いだけ前へ伸ばす。
// 跳んでいる間は判定しない(他の敵の上を越える)
EnemyHorde::Collider EnemyHorde::colliderOf(const Enemy &enemy) const
{
	const Kind &kind = kinds_[enemy.kind];
	Collider collider;
	collider.mobility = enemy.pinned ? kPinnedMobility : (enemy.moving ? kWalkMobility : kIdleMobility);
	float fallen = 0.0f;
	if(kind.creature){
		using Motion = CreatureDriver::Motion;
		const CreatureDriver::State &state = enemy.creature;
		const float jump = kind.creature->jumpProgress(state);
		collider.solid = jump <= 0.0f || jump >= 1.0f;
		fallen = kind.creature->fallen(state);
		if((state.motion == Motion::AttackStand || state.motion == Motion::AttackJump) && !enemy.pinned){
			collider.mobility = kAttackMobility;
		}
	}
	const float extra = std::max(kind.def.box.fallLength, 0.0f) * fallen;
	const float halfWidth = 0.5f * kind.boxWidth, halfLength = 0.5f * (kind.boxLength + extra);
	const float forwardX = std::sin(enemy.yaw), forwardZ = std::cos(enemy.yaw); // 体の正面の向き
	collider.offsetX = 0.5f * extra * forwardX;
	collider.offsetZ = 0.5f * extra * forwardZ;
	collider.halfX = std::fabs(forwardZ) * halfWidth + std::fabs(forwardX) * halfLength;
	collider.halfZ = std::fabs(forwardX) * halfWidth + std::fabs(forwardZ) * halfLength;
	collider.bottom = enemy.y;
	collider.top = enemy.y + kind.boxHeight;
	return collider;
}

// 格子のセルごとに個体を並べる(数え上げのソート)
void EnemyHorde::buildGrid()
{
	const size_t n = enemies_.size();
	std::fill(cellStart_.begin(), cellStart_.end(), 0u);
	for(size_t i = 0; i < n; ++i){
		const int cx = std::clamp(static_cast<int>(enemies_[i].x / gridCell_), 0, gridW_ - 1);
		const int cz = std::clamp(static_cast<int>(enemies_[i].z / gridCell_), 0, gridD_ - 1);
		cellOf_[i] = static_cast<uint32_t>(cz * gridW_ + cx);
		++cellStart_[cellOf_[i] + 1];
	}
	for(size_t c = 1; c < cellStart_.size(); ++c){
		cellStart_[c] += cellStart_[c - 1];
	}
	// cellStart_[c] を詰める位置として使い、詰め終わったら元(セルの先頭)へ戻す
	for(size_t i = 0; i < n; ++i){
		cellItems_[cellStart_[cellOf_[i]]++] = static_cast<uint32_t>(i);
	}
	for(size_t c = cellStart_.size() - 1; c > 0; --c){
		cellStart_[c] = cellStart_[c - 1];
	}
	cellStart_[0] = 0;
}

// 敵同士の箱の重なりを解く: 重なっている2体を、重なりの浅い軸(XかZ)の向きへ、重なりの分だけ離す(mobility の比で分ける)。
// 格子の隣のセルまでだけ調べ、押し出した位置を、その場で次の組に使う(数回くり返す)。最後に、押し出した先へ、地形(フィールドの縁・歩けない所・急な坂)に
// 沿って動かし直す。地形で止められた個体は、次のフレームでは押し出されにくくする(相手の方が動く)
void EnemyHorde::resolveOverlaps(const field::FieldMap &map, const field::MovementRules &rules)
{
	const size_t n = enemies_.size();
	for(size_t i = 0; i < n; ++i){
		colliders_[i] = colliderOf(enemies_[i]);
		fromX_[i] = enemies_[i].x;
		fromZ_[i] = enemies_[i].z;
	}
	buildGrid();
	for(int iteration = 0; iteration < kOverlapIterations; ++iteration){
		for(size_t i = 0; i < n; ++i){
			const Collider &a = colliders_[i];
			if(!a.solid){
				continue;
			}
			Enemy &ea = enemies_[i];
			const int cx = static_cast<int>(cellOf_[i] % static_cast<uint32_t>(gridW_)), cz = static_cast<int>(cellOf_[i] / static_cast<uint32_t>(gridW_));
			for(int z = std::max(cz - 1, 0); z <= std::min(cz + 1, gridD_ - 1); ++z){
				for(int x = std::max(cx - 1, 0); x <= std::min(cx + 1, gridW_ - 1); ++x){
					const size_t cell = static_cast<size_t>(z) * gridW_ + x;
					for(uint32_t k = cellStart_[cell]; k < cellStart_[cell + 1]; ++k){
						const uint32_t j = cellItems_[k];
						const Collider &b = colliders_[j];
						if(j <= i || !b.solid || a.top <= b.bottom || b.top <= a.bottom){
							continue;
						}
						Enemy &eb = enemies_[j];
						const float dx = (eb.x + b.offsetX) - (ea.x + a.offsetX), dz = (eb.z + b.offsetZ) - (ea.z + a.offsetZ);
						const float overlapX = a.halfX + b.halfX - std::fabs(dx), overlapZ = a.halfZ + b.halfZ - std::fabs(dz);
						if(overlapX <= 0.0f || overlapZ <= 0.0f){
							continue;
						}
						const float share = a.mobility / (a.mobility + b.mobility); // a が動く割合
						if(overlapX < overlapZ){
							const float push = dx < 0.0f ? -overlapX : overlapX;
							ea.x -= push * share;
							eb.x += push * (1.0f - share);
						}
						else{
							const float push = dz < 0.0f ? -overlapZ : overlapZ;
							ea.z -= push * share;
							eb.z += push * (1.0f - share);
						}
					}
				}
			}
		}
	}
	const float fieldW = map.width() * map.cellSize(), fieldD = map.depth() * map.cellSize();
	for(size_t i = 0; i < n; ++i){
		Enemy &enemy = enemies_[i];
		enemy.pinned = false;
		if(enemy.x == fromX_[i] && enemy.z == fromZ_[i]){
			continue;
		}
		const float toX = std::clamp(enemy.x, kRadius, std::max(kRadius, fieldW - kRadius));
		const float toZ = std::clamp(enemy.z, kRadius, std::max(kRadius, fieldD - kRadius));
		enemy.x = fromX_[i];
		enemy.z = fromZ_[i];
		field::moveOnField(map, rules, kRadius, enemy.x, enemy.z, toX, toZ);
		enemy.pinned = std::fabs(enemy.x - toX) + std::fabs(enemy.z - toZ) > kPinnedSlip;
		enemy.y = map.heightAt(enemy.x, enemy.z);
	}
}

// (x, z) に置いた self の箱が、他の敵の箱(跳んでいる敵を除く)と重なる面積の合計(着地点を選ぶ。格子は前の重なりの解消のもの)
float EnemyHorde::overlapAt(const Enemy &self, float x, float z) const
{
	const Collider a = colliderOf(self);
	const int cx = std::clamp(static_cast<int>(x / gridCell_), 0, gridW_ - 1);
	const int cz = std::clamp(static_cast<int>(z / gridCell_), 0, gridD_ - 1);
	float total = 0.0f;
	for(int gz = std::max(cz - 1, 0); gz <= std::min(cz + 1, gridD_ - 1); ++gz){
		for(int gx = std::max(cx - 1, 0); gx <= std::min(cx + 1, gridW_ - 1); ++gx){
			const size_t cell = static_cast<size_t>(gz) * gridW_ + gx;
			for(uint32_t k = cellStart_[cell]; k < cellStart_[cell + 1]; ++k){
				const Enemy &other = enemies_[cellItems_[k]];
				const Collider &b = colliders_[cellItems_[k]];
				if(&other == &self || !b.solid){
					continue;
				}
				const float overlapX = a.halfX + b.halfX - std::fabs(other.x + b.offsetX - (x + a.offsetX));
				const float overlapZ = a.halfZ + b.halfZ - std::fabs(other.z + b.offsetZ - (z + a.offsetZ));
				if(overlapX > 0.0f && overlapZ > 0.0f){
					total += overlapX * overlapZ;
				}
			}
		}
	}
	return total;
}

// 跳ぶ攻撃の着地点: プレイヤーの手前 landGap(±landGapJitter)の所で、プレイヤーから見て敵のいる向きを ±landAngleJitter 回した向き(跳ぶ距離は distance まで)。
// landTries 個の候補から、他の敵の箱と重ならない(全部重なるなら、重なりのいちばん小さい)候補を選ぶ
void EnemyHorde::chooseLanding(const Kind &kind, Enemy &enemy, float targetX, float targetZ, const field::FieldMap &map)
{
	const auto &j = kind.def.creatureMotion.jump;
	std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
	const float fieldW = map.width() * map.cellSize(), fieldD = map.depth() * map.cellSize();
	const float away = std::atan2(enemy.x - targetX, enemy.z - targetZ);
	float best = std::numeric_limits<float>::max();
	for(int t = 0; t < std::max(j.landTries, 1) && best > 0.0f; ++t){
		const float gap = std::max(j.landGap + j.landGapJitter * jitter(rng_), 0.0f);
		const float angle = away + j.landAngleJitter * jitter(rng_);
		float x = targetX + gap * std::sin(angle), z = targetZ + gap * std::cos(angle);
		const float travelX = x - enemy.x, travelZ = z - enemy.z, travel = std::hypot(travelX, travelZ);
		if(travel > j.distance){
			const float k = std::max(j.distance, 0.0f) / travel;
			x = enemy.x + travelX * k;
			z = enemy.z + travelZ * k;
		}
		x = std::clamp(x, kRadius, std::max(kRadius, fieldW - kRadius));
		z = std::clamp(z, kRadius, std::max(kRadius, fieldD - kRadius));
		const float overlap = overlapAt(enemy, x, z);
		if(overlap < best){
			best = overlap;
			enemy.jumpToX = x;
			enemy.jumpToZ = z;
		}
	}
}

void EnemyHorde::update(float dt, const field::FieldMap &map, const field::MovementRules &rules, const field::PropCollision &props, float targetX,
	float targetZ)
{
	if(enemies_.empty() || dt <= 0.0f){
		return;
	}
	const float fieldW = map.width() * map.cellSize(), fieldD = map.depth() * map.cellSize();
	for(size_t i = 0; i < enemies_.size(); ++i){
		Enemy &enemy = enemies_[i];
		const Kind &kind = kinds_[enemy.kind];
		const EnemyType &def = kind.def;
		const float dx = targetX - enemy.x, dz = targetZ - enemy.z;
		const float distance = std::sqrt(dx * dx + dz * dz);
		if(enemy.moving ? distance <= enemy.stopDistance : distance > enemy.stopDistance + kStopHysteresis){
			enemy.moving = !enemy.moving;
		}
		// 丸い生き物の体が攻撃している間は、攻撃の動きだけ(歩き・押し合い・向きの変化は止める)
		if(!kind.creature || !updateCreature(kind, enemy, dt, distance, dx, dz, map, rules, props)){
			const auto &creatureMotion = def.creatureMotion;
			const float baseSpeed = !kind.creature ? def.speed : (enemy.fast ? creatureMotion.walkFast.speed : creatureMotion.walkSlow.speed);
			const float speed = baseSpeed * enemy.speedScale;
			float vx = 0.0f, vz = 0.0f;
			if(enemy.moving && distance > 1e-4f){
				vx = dx / distance * speed;
				vz = dz / distance * speed;
			}
			if(vx != 0.0f || vz != 0.0f){
				const float toX = std::clamp(enemy.x + vx * dt, kRadius, std::max(kRadius, fieldW - kRadius));
				const float toZ = std::clamp(enemy.z + vz * dt, kRadius, std::max(kRadius, fieldD - kRadius));
				field::moveOnField(map, rules, kRadius, enemy.x, enemy.z, toX, toZ);
				enemy.y = map.heightAt(enemy.x, enemy.z);
			}
			// 向き: いつもプレイヤーの方へ(重なりの押し出しで横へずれても、横歩きに見えるだけで、体はプレイヤーを向く)
			if(distance > 1e-4f){
				enemy.yaw = approachAngle(enemy.yaw, std::atan2(dx, dz), def.turnSpeed * dt);
			}
			if(kind.creature){
				// 丸い生き物の体: 歩いている間は、早歩きか、ゆっくり歩き。止まったら待機
				using Motion = CreatureDriver::Motion;
				kind.creature->play(enemy.creature, enemy.moving ? (enemy.fast ? Motion::WalkFast : Motion::WalkSlow) : Motion::Idle);
				kind.creature->advance(enemy.creature, dt, speed);
			}
			else{
				// PMXの歩きの振れ幅と位相: 足の運びの速さは、速さ÷1周期で進む距離(速い個体ほど、足も速く動く)
				const float targetWeight = enemy.moving ? 1.0f : 0.0f;
				enemy.walkWeight += (targetWeight - enemy.walkWeight) * std::min(1.0f, def.gait.blend * dt);
				if(enemy.walkWeight > kStillWeight){
					enemy.phase = std::fmod(enemy.phase + dt * speed / def.gait.stride, 1.0f);
				}
			}
		}
		// 姿勢の更新の時期: 遠い個体は間引く(near までは毎フレーム、far で interval 秒ごと)。PMXで止まりきって、その姿勢を当て済みなら、更新しない。
		// 姿勢を当てるのは draw で、画面に見えている個体だけ(見えない個体は、見えたときに当てる)
		const float span = std::max(def.lod.far - def.lod.near, 1e-3f);
		const float interval = def.lod.interval * std::clamp((distance - def.lod.near) / span, 0.0f, 1.0f);
		enemy.poseTimer += dt;
		enemy.poseDue = enemy.poseDue || (enemy.poseTimer >= interval && !(enemy.walkWeight <= kStillWeight && enemy.posedStill));
	}
	resolveOverlaps(map, rules);
}

// 跳ぶ軌道を決める: 今の位置から、着地点(enemy.jumpToX/Z。chooseLanding)へ。着地点は、立てる所(歩けるタイルの上・急すぎない坂・置物の外)に限る。
// 頂点の高さ(跳び立つ点と着地点の地面を結ぶ直線から)は、跳ぶ距離に比例させ(height × 距離 ÷ distance。minHeight 以上)、経路の上の地面と置物のてっぺん
// (置物は clearance を空ける)を越える高さまで上げる。maxHeight を超えるか、着地点に立てなければ、距離を縮めて試し直す(minDistance より短くなったら、跳べない)。
// 経路の途中は、歩けない所(水など)でも越えてよい。滞空時間は、頂点の高さと重力から(√(8 × 高さ ÷ 重力))。
// 跳べれば true(跳び立つ点・着地点・高さ・空中の区間の時刻の進み方を enemy に入れる)。hopInPlace なら、跳べないときは、その場で minHeight だけ跳ねる(true)
bool EnemyHorde::planJump(const Kind &kind, Enemy &enemy, const field::FieldMap &map, const field::MovementRules &rules, const field::PropCollision &props,
	bool hopInPlace)
{
	constexpr float kShorten[] = {1.0f, 0.8f, 0.6f, 0.4f}; // 距離を縮めて試す割合
	const auto &j = kind.def.creatureMotion.jump;
	const float fromX = enemy.x, fromZ = enemy.z, fromY = map.heightAt(fromX, fromZ);
	const float dx = enemy.jumpToX - fromX, dz = enemy.jumpToZ - fromZ, full = std::hypot(dx, dz);
	const float step = std::max(j.probeStep, 0.05f);
	auto decide = [&](float toX, float toZ, float toY, float height){
		enemy.jumpFromX = fromX;
		enemy.jumpFromZ = fromZ;
		enemy.jumpFromY = fromY;
		enemy.jumpToX = toX;
		enemy.jumpToZ = toZ;
		enemy.jumpToY = toY;
		enemy.jumpHeight = height;
		const CreatureDriver::JumpPhases phases = kind.creature->jumpPhases();
		const float air = std::sqrt(8.0f * height / std::max(j.gravity, 0.1f));
		enemy.jumpAirRate = std::max(phases.airEnd - phases.launchEnd, 0.0f) / std::max(air, 1e-3f);
	};
	for(const float f : kShorten){
		const float d = full * f;
		if(d < std::max(j.minDistance, 0.0f)){
			break;
		}
		const float toX = fromX + dx * f, toZ = fromZ + dz * f;
		float normal[3];
		map.normalAt(toX, toZ, normal);
		const float slope = std::hypot(normal[0], normal[2]) / std::max(normal[1], 1e-3f);
		if(!field::canStandAt(map, rules, toX, toZ, kRadius) || slope > rules.maxSlope || props.overlaps(toX, toZ, kind.bodyRadius)){
			continue;
		}
		const float toY = map.heightAt(toX, toZ);
		// 距離に比例した高さ(minHeight 以上)。高低差があっても、頂点が2点の間に来る(登りながら着地しない)よう、高低差の半分以上
		float height = std::max({j.minHeight, j.height * d / std::max(j.distance, 0.1f), 0.5f * std::fabs(toY - fromY)});
		const int samples = std::max(2, static_cast<int>(std::ceil(d / step)));
		for(int i = 1; i < samples; ++i){
			// 放物線は、直線からの高さが 4 × 頂点の高さ × p(1-p)。障害物が直線より above だけ高ければ、頂点の高さは above ÷ 4p(1-p) 以上
			const float p = static_cast<float>(i) / static_cast<float>(samples);
			const float x = fromX + (toX - fromX) * p, z = fromZ + (toZ - fromZ) * p;
			const float line = fromY + (toY - fromY) * p;
			const float above = std::max(map.heightAt(x, z), props.topAt(x, z, kind.bodyRadius) + j.clearance) - line;
			if(above > 0.0f){
				height = std::max(height, above / (4.0f * p * (1.0f - p)));
			}
		}
		if(height <= j.maxHeight){
			decide(toX, toZ, toY, height);
			return true;
		}
	}
	if(hopInPlace){
		decide(fromX, fromZ, fromY, std::max(j.minHeight, 0.0f));
		return true;
	}
	return false;
}

// 丸い生き物の体の攻撃。攻撃している間は true(跳ぶ攻撃なら、跳び立つ点から着地点(chooseLanding・planJump)へ、位置を放物線で動かす)。
// 攻撃していなければ、時期(cooldown)になったら、プレイヤーとの距離に応じて、確率で攻撃を始める(跳ぶ攻撃は、跳べる経路が無ければ始めない)。
// 攻撃が終わったら false(呼び出し側が歩き・待機に戻す)
bool EnemyHorde::updateCreature(const Kind &kind, Enemy &enemy, float dt, float distance, float dx, float dz, const field::FieldMap &map,
	const field::MovementRules &rules, const field::PropCollision &props)
{
	using Motion = CreatureDriver::Motion;
	const CreatureDriver &creature = *kind.creature;
	CreatureDriver::State &state = enemy.creature;
	const EnemyType::Behavior &b = kind.def.behavior;
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	if(state.motion == Motion::AttackStand || state.motion == Motion::AttackJump){
		if(creature.finished(state)){
			enemy.cooldown = b.cooldown + b.cooldownJitter * unit(rng_);
			return false;
		}
	}
	else{
		enemy.cooldown -= dt;
		if(enemy.cooldown > 0.0f){
			return false;
		}
		const float chance = unit(rng_);
		const bool stand = distance <= b.closeRange && chance < b.standChance;
		bool jump = !stand && distance > b.jumpMin && distance <= b.jumpRange && chance < b.jumpChance;
		if(jump){
			chooseLanding(kind, enemy, enemy.x + dx, enemy.z + dz, map);
			jump = planJump(kind, enemy, map, rules, props, false); // 経路が塞がれていれば、跳ばずに歩く
		}
		if(!stand && !jump){
			enemy.cooldown = b.retry;
			return false;
		}
		creature.play(state, stand ? Motion::AttackStand : Motion::AttackJump);
		enemy.jumping = jump;
		enemy.jumpPlanned = false;
	}
	if(state.motion != Motion::AttackJump || !enemy.jumping){
		creature.advance(state, dt, 0.0f);
		return true;
	}
	// 跳ぶ攻撃: 動きの時刻を進める(空中の区間は、決めた滞空時間に合わせて伸縮する。advance は時刻に rate を掛けるので、その分を割って渡す)
	const CreatureDriver::JumpPhases phases = creature.jumpPhases();
	const float time = advanceJumpTime(state.actionTime, dt, state.rate, phases.launchEnd, phases.airEnd, enemy.jumpAirRate);
	creature.advance(state, (time - state.actionTime) / std::max(state.rate, 1e-3f), 0.0f);
	// 踏み切りの直前(溜めの終わり)に、今の位置(溜めの間に、重なりの押し出しで動いた分)から、軌道を決め直す。跳べなくなっていたら、その場で跳ねる
	if(!enemy.jumpPlanned && state.actionTime >= phases.crouchEnd){
		enemy.jumpPlanned = true;
		planJump(kind, enemy, map, rules, props, true);
	}
	const float p = creature.jumpProgress(state);
	if(p <= 0.0f){
		// 跳び立つまでは、跳び立つ点を今の位置に合わせ、着地点の方へ向く
		enemy.jumpFromX = enemy.x;
		enemy.jumpFromZ = enemy.z;
		enemy.jumpFromY = enemy.y;
		const float toX = enemy.jumpToX - enemy.x, toZ = enemy.jumpToZ - enemy.z;
		if(toX * toX + toZ * toZ > 1e-4f){
			enemy.yaw = approachAngle(enemy.yaw, std::atan2(toX, toZ), kind.def.turnSpeed * dt);
		}
		return true;
	}
	// 空中: 位置は、跳び立つ点から着地点へ直線で、高さは、2点の地面を結ぶ直線 + 放物線(経路は planJump で確かめてあるので、地形の制限は掛けない)。
	// 着地した後は動かさない(重なりの押し出しで動いた位置を、着地点へ引き戻さない)
	enemy.jumping = p < 1.0f;
	const float fieldW = map.width() * map.cellSize(), fieldD = map.depth() * map.cellSize();
	enemy.x = std::clamp(enemy.jumpFromX + (enemy.jumpToX - enemy.jumpFromX) * p, kRadius, std::max(kRadius, fieldW - kRadius));
	enemy.z = std::clamp(enemy.jumpFromZ + (enemy.jumpToZ - enemy.jumpFromZ) * p, kRadius, std::max(kRadius, fieldD - kRadius));
	enemy.y = enemy.jumping ? enemy.jumpFromY + (enemy.jumpToY - enemy.jumpFromY) * p + enemy.jumpHeight * 4.0f * p * (1.0f - p)
		: map.heightAt(enemy.x, enemy.z);
	return true;
}

void EnemyHorde::applyPose(const Kind &kind, Enemy &enemy) const
{
	if(kind.creature){
		kind.creature->apply(*enemy.model->skeleton(), enemy.creature);
		enemy.model->updatePose(0.0f);
	}
	else{
		applyGait(kind, enemy);
	}
}

// PMXの手続き的な歩き: 位相 p(0〜1)の正弦で、脚・腕を前後に振り、膝・肘を曲げ、体を上下・左右に揺らす。振れ幅は walkWeight を掛ける。
// 回転はPMXの元の座標系(左手系。正面が-Z)のボーンの局所の回転(休止ポーズの向きは単位)で、X軸まわりの + は、下を向いた骨の先を前(-Z)へ振る
void EnemyHorde::applyGait(const Kind &kind, Enemy &enemy) const
{
	auto *skeleton = enemy.model->skeleton();
	const EnemyType::Gait &g = kind.def.gait;
	const float w = enemy.walkWeight;
	const float p = enemy.phase * kTwoPi;
	const float s = std::sin(p), c = std::cos(p);
	skeleton->resetPose();
	// 脚: 右脚は sin で振り(左は逆)、前へ振り出す間(振る角度が増えている間 = cos の向き)だけ、膝を曲げて足を上げる。
	// 足首は、脚の傾きを ankle の割合だけ打ち消して、足の裏を地面に沿わせる
	const float legAngle[2] = {g.legSwing * s * w, -g.legSwing * s * w};
	const float kneeAngle[2] = {-g.kneeBend * std::max(0.0f, c) * w, -g.kneeBend * std::max(0.0f, -c) * w};
	for(int side = 0; side < 2; ++side){
		rotate(*skeleton, kind.leg[side], rotationX(legAngle[side]));
		rotate(*skeleton, kind.knee[side], rotationX(kneeAngle[side]));
		rotate(*skeleton, kind.ankle[side], rotationX(-(legAngle[side] + kneeAngle[side]) * g.ankle));
	}
	// 腕: 脚と逆に振る。Aポーズの腕は、体の横へ armDown だけ下ろしてから振る。肘は少し曲げておき、腕が前へ振れたときに、さらに曲げる
	const float armAngle[2] = {-g.armSwing * s * w, g.armSwing * s * w};
	for(int side = 0; side < 2; ++side){
		rotate(*skeleton, kind.arm[side], rotationX(armAngle[side]) * rotationZ(g.armDown * kind.armSide[side]));
		const float forward = g.armSwing > 0.0f ? std::max(0.0f, armAngle[side] / g.armSwing) : 0.0f;
		rotate(*skeleton, kind.elbow[side], rotationX(g.elbowBend * (1.0f + 0.5f * forward)));
	}
	// 体: 上半身は前へ傾けて、腕の振りに合わせてひねる(下半身は逆へ半分)
	rotate(*skeleton, kind.upper, rotationX(-g.lean * w) * rotationY(g.twist * s * w));
	rotate(*skeleton, kind.lower, rotationY(-0.5f * g.twist * s * w));
	// 腰の上下: 脚を振ると、足が地面より上がる分(脚の長さ×(1-cos 角度))だけ下げる(1歩ごとに2回上下する)。それに bob の揺れを足す。左右は sway。長さはモデルの単位へ直す
	if(kind.center >= 0){
		const float toModel = 1.0f / kind.scale;
		const float drop = kind.legLength * (1.0f - std::cos(legAngle[0]));
		const float bob = g.bob * std::cos(2.0f * p) * w * toModel;
		const float sway = g.sway * c * w * toModel;
		skeleton->setBoneTranslation(kind.center, {sway, bob - drop, 0.0f});
	}
	enemy.model->updatePose(0.0f);
}

void EnemyHorde::draw(SDL_::VulkanWindow &window, const geo::Matrix4x4f &viewProj, const BlobShadow &shadow, const geo::Vector3f &eye)
{
	// 視錐台の6つの面(clip = viewProj * p の、-w≦x,y≦w、0≦z≦w。列優先の行列の行から作る)。各個体を囲む球が、どれかの面の外なら、見えない
	const float *m = viewProj.data();
	float planes[6][4];
	for(int k = 0; k < 4; ++k){
		const float r0 = m[k * 4], r1 = m[k * 4 + 1], r2 = m[k * 4 + 2], r3 = m[k * 4 + 3];
		planes[0][k] = r3 + r0;
		planes[1][k] = r3 - r0;
		planes[2][k] = r3 + r1;
		planes[3][k] = r3 - r1;
		planes[4][k] = r2;
		planes[5][k] = r3 - r2;
	}
	for(auto &plane : planes){
		const float length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
		for(float &value : plane){
			value /= std::max(length, 1e-6f);
		}
	}
	const float eyeX = eye.getX(), eyeY = eye.getY(), eyeZ = eye.getZ();
	for(size_t i = 0; i < enemies_.size(); ++i){
		Enemy &enemy = enemies_[i];
		const Kind &kind = kinds_[enemy.kind];
		const float center[3] = {enemy.x, enemy.y + kind.boundY, enemy.z};
		const bool visible = std::none_of(std::begin(planes), std::end(planes), [&](const float (&plane)[4]){
			return plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2] + plane[3] < -kind.boundRadius;
		});
		if(!visible){
			continue;
		}
		// カメラ(目の位置)から箱までが cameraCullRadius より近い敵は、画面を覆わないよう描かない(影も)
		const Collider &box = colliders_[i];
		const float gapX = std::max(std::fabs(eyeX - (enemy.x + box.offsetX)) - box.halfX, 0.0f);
		const float gapZ = std::max(std::fabs(eyeZ - (enemy.z + box.offsetZ)) - box.halfZ, 0.0f);
		const float gapY = std::max({box.bottom - eyeY, eyeY - box.top, 0.0f});
		const float cull = kind.def.cameraCullRadius;
		if(gapX * gapX + gapY * gapY + gapZ * gapZ < cull * cull){
			continue;
		}
		if(enemy.poseDue){
			applyPose(kind, enemy);
			enemy.poseDue = false;
			enemy.poseTimer = 0.0f;
			enemy.posedStill = !kind.creature && enemy.walkWeight <= kStillWeight; // 丸い生き物の体は、止まっても動き続ける
		}
		transform_.setPos(geo::Vector3f(enemy.x, enemy.y, enemy.z));
		transform_.setRotation(geo::Quaternionf::createRotater(enemy.yaw, geo::Vector3f(0.0f, 1.0f, 0.0f)));
		// 倒れ込み・着地の潰れ(地面 = モデルの Y=0 を基準に、高さを縮めて前後・左右へ広げる。地面にめり込まず、浮かない)
		const CreatureDriver::BodyScale squash = kind.creature ? kind.creature->bodyScale(enemy.creature) : CreatureDriver::BodyScale{};
		transform_.setScale(geo::Vector3f(kind.scale * squash.horizontal, kind.scale * squash.vertical, kind.scale * squash.horizontal));
		window.draw(enemy.model, viewProj, transform_.getMatrix());
		shadow.draw(window, viewProj, enemy.x, enemy.y, enemy.z, kind.def.shadowRadius, kind.def.shadowOpacity);
	}
}

} // namespace game
