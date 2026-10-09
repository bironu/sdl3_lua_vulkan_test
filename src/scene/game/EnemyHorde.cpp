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

void readClipEntry(const sol::optional<sol::table> &motions, const char *key, CreatureClipMotion::Entry &entry)
{
	if(!motions){
		return;
	}
	const sol::optional<sol::table> t = (*motions)[key];
	readField(t, "clip", entry.clip);
	readField(t, "loop", entry.loop);
	readField(t, "stride", entry.stride);
}

// クリップで動かすときの表(enemies.lua の motions・rootMotionBones)。path はクリップを読むファイル
void readClipMotion(const sol::table &t, const sol::optional<sol::table> &motions, const std::string &path, CreatureClipMotion &clips)
{
	clips.path = path;
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
	readField(stand, "fallStart", clips.stand.fallStart);
	readField(stand, "fallEnd", clips.stand.fallEnd);
	readField(stand, "recoverStart", clips.stand.recoverStart);
	readField(stand, "recoverEnd", clips.stand.recoverEnd);
	readField(jump, "crouchEnd", clips.jump.crouchEnd);
	readField(jump, "launchEnd", clips.jump.launchEnd);
	readField(jump, "airEnd", clips.jump.airEnd);
	if(const sol::optional<sol::table> bones = t["rootMotionBones"]){
		for(size_t i = 1; i <= bones->size(); ++i){
			clips.rootMotionBones.push_back(bones->get_or(static_cast<int>(i), std::string()));
		}
	}
}

// クリップで動かす体の行動と、跳ぶ攻撃の軌道(enemies.lua の behavior・jump の表)
void readBehavior(const sol::optional<sol::table> &behavior, const sol::optional<sol::table> &jump, EnemyType &type)
{
	auto &b = type.behavior;
	readField(behavior, "fastRatio", b.fastRatio);
	readField(behavior, "fastSpeed", b.fastSpeed);
	readField(behavior, "slowSpeed", b.slowSpeed);
	readField(behavior, "closeRange", b.closeRange);
	readField(behavior, "jumpMin", b.jumpMin);
	readField(behavior, "jumpRange", b.jumpRange);
	readField(behavior, "standChance", b.standChance);
	readField(behavior, "jumpChance", b.jumpChance);
	readField(behavior, "cooldown", b.cooldown);
	readField(behavior, "cooldownJitter", b.cooldownJitter);
	readField(behavior, "retry", b.retry);
	readField(behavior, "timeJitter", b.timeJitter);
	auto &j = type.jump;
	readField(jump, "height", j.height);
	readField(jump, "distance", j.distance);
	readField(jump, "minDistance", j.minDistance);
	readField(jump, "minHeight", j.minHeight);
	readField(jump, "maxHeight", j.maxHeight);
	readField(jump, "gravity", j.gravity);
	readField(jump, "clearance", j.clearance);
	readField(jump, "probeStep", j.probeStep);
	readField(jump, "landGap", j.landGap);
	readField(jump, "landGapJitter", j.landGapJitter);
	readField(jump, "landAngleJitter", j.landAngleJitter);
	readField(jump, "landTries", j.landTries);
}

// PMXの歩き(enemies.lua の gait・bones の表。ボーンはMMDの名前)
void readGait(const sol::optional<sol::table> &gait, const sol::optional<sol::table> &bones, EnemyType &type)
{
	readField(gait, "stride", type.gait.stride);
	readField(gait, "legSwing", type.gait.legSwing);
	readField(gait, "kneeBend", type.gait.kneeBend);
	readField(gait, "ankle", type.gait.ankle);
	readField(gait, "armSwing", type.gait.armSwing);
	readField(gait, "armDown", type.gait.armDown);
	readField(gait, "elbowBend", type.gait.elbowBend);
	readField(gait, "twist", type.gait.twist);
	readField(gait, "lean", type.gait.lean);
	readField(gait, "bob", type.gait.bob);
	readField(gait, "sway", type.gait.sway);
	readField(gait, "blend", type.gait.blend);
	type.gait.stride = std::max(type.gait.stride, 0.05f);
	readField(bones, "center", type.bones.center);
	readField(bones, "upper", type.bones.upper);
	readField(bones, "lower", type.bones.lower);
	readField(bones, "rightLeg", type.bones.rightLeg);
	readField(bones, "leftLeg", type.bones.leftLeg);
	readField(bones, "rightKnee", type.bones.rightKnee);
	readField(bones, "leftKnee", type.bones.leftKnee);
	readField(bones, "rightAnkle", type.bones.rightAnkle);
	readField(bones, "leftAnkle", type.bones.leftAnkle);
	readField(bones, "rightArm", type.bones.rightArm);
	readField(bones, "leftArm", type.bones.leftArm);
	readField(bones, "rightElbow", type.bones.rightElbow);
	readField(bones, "leftElbow", type.bones.leftElbow);
}

// 休止ポーズのメッシュの範囲(モデルの単位。全部の頂点を囲む、軸に平行な箱)
struct MeshBounds
{
	float min[3] = {0.0f, 0.0f, 0.0f};
	float max[3] = {0.0f, 0.0f, 0.0f};
};
MeshBounds measureMesh(const std::vector<model::ModelVertex> &vertices)
{
	MeshBounds bounds;
	for(size_t i = 0; i < vertices.size(); ++i){
		for(int k = 0; k < 3; ++k){
			const float v = vertices[i].position[k];
			bounds.min[k] = i == 0 ? v : std::min(bounds.min[k], v);
			bounds.max[k] = i == 0 ? v : std::max(bounds.max[k], v);
		}
	}
	return bounds;
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
		readField(entry, "model", type.model);
		if(type.model.empty()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy table: '%s' has no model (%s)", type.name.c_str(), relativePath.c_str());
			continue;
		}
		type.count = std::max(t.get_or("count", 0), 0);
		readField(entry, "seed", type.seed);
		readField(entry, "height", type.height);
		readField(entry, "length", type.length);
		readField(entry, "speed", type.speed);
		readField(entry, "speedJitter", type.speedJitter);
		readField(entry, "turnSpeed", type.turnSpeed);
		readField(entry, "spawnMinRadius", type.spawnMinRadius);
		readField(entry, "spawnRadius", type.spawnRadius);
		readField(entry, "stopDistance", type.stopDistance);
		readField(entry, "stopJitter", type.stopJitter);
		readField(entry, "cameraCullRadius", type.cameraCullRadius);
		readField(entry, "shadowRadius", type.shadowRadius);
		readField(entry, "shadowOpacity", type.shadowOpacity);
		readField(entry, "motionBlend", type.motionBlend);
		const sol::optional<sol::table> box = t["box"], lod = t["lod"], motions = t["motions"];
		readField(box, "width", type.box.width);
		readField(box, "length", type.box.length);
		readField(box, "height", type.box.height);
		readField(box, "fallLength", type.box.fallLength);
		readField(lod, "near", type.lod.near);
		readField(lod, "far", type.lod.far);
		readField(lod, "interval", type.lod.interval);
		// motions か clips があれば、クリップで動かす(クリップの既定は、model と同じファイル)。無ければ、PMXの歩き
		const sol::optional<std::string> clips = t["clips"];
		if(motions || clips){
			readClipMotion(t, motions, clips.value_or(type.model), type.clipMotion);
			readBehavior(t["behavior"], t["jump"], type);
		}
		else{
			readGait(t["gait"], t["bones"], type);
		}
		result.push_back(std::move(type));
	}
	return result;
}

// モデルから、大きさ・地面に置く高さ・当たりの箱・影・見えるかの判定の球と、動き(クリップか、PMXの歩きのボーン)を求める。
// 形から決まる値は、休止ポーズのメッシュの範囲から(表に書いた値があれば、そちら)。ボーンの無いモデル・クリップが読めないモデルはfalse
bool EnemyHorde::setupKind(Kind &kind, VulkanModel &model)
{
	auto *skeleton = model.skeleton();
	const auto &name = kind.def.name;
	if(!skeleton){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy '%s': the model has no bones", name.c_str());
		return false;
	}
	const auto &vertices = model.data().vertices;
	const MeshBounds mesh = measureMesh(vertices);
	const float width = mesh.max[0] - mesh.min[0], height = mesh.max[1] - mesh.min[1], length = mesh.max[2] - mesh.min[2];
	// 大きさ: 体長(前後の長さ)か、背の高さに合わせる(どちらも無ければ、モデルの単位のまま = glb はメートル)
	const auto &def = kind.def;
	kind.scale = def.length > 0.0f && length > 0.0f ? def.length / length : (def.height > 0.0f && height > 0.0f ? def.height / height : 1.0f);
	kind.groundY = mesh.min[1];
	// 当たりの箱: 表に無ければ、メッシュの範囲。中心は、メッシュの左右・前後の範囲の中心(モデルの正面は -Z)
	const float scale = kind.scale;
	kind.boxWidth = def.box.width > 0.0f ? def.box.width : width * scale;
	kind.boxLength = def.box.length > 0.0f ? def.box.length : length * scale;
	kind.boxHeight = def.box.height > 0.0f ? def.box.height : height * scale;
	kind.boxSide = 0.5f * (mesh.min[0] + mesh.max[0]) * scale;
	kind.boxForward = -0.5f * (mesh.min[2] + mesh.max[2]) * scale;
	kind.shadowRadius = def.shadowRadius > 0.0f ? def.shadowRadius : 0.25f * (kind.boxWidth + kind.boxLength);
	// 見えるかの判定の球: 足元の真上、高さの半分を中心に、全部の頂点を囲む(手足を動かした分の余裕を足す)
	const float midY = 0.5f * (mesh.min[1] + mesh.max[1]);
	float radius2 = 0.0f;
	for(const auto &vertex : vertices){
		const float dy = vertex.position[1] - midY;
		radius2 = std::max(radius2, vertex.position[0] * vertex.position[0] + dy * dy + vertex.position[2] * vertex.position[2]);
	}
	kind.boundY = (midY - kind.groundY) * scale;
	kind.boundRadius = std::max(1.2f * std::sqrt(radius2) * scale, kind.shadowRadius);
	kind.bodyRadius = 0.5f * std::min(kind.boxWidth, kind.boxLength);
	SDL_Log("enemy '%s': model %.2f x %.2f x %.2f units (w x h x l) -> scale %.4f (%.2f m long), %zu vertices, %zu bones, box %.2f x %.2f x %.2f m, shadow %.2f m",
		name.c_str(), width, height, length, scale, length * scale, vertices.size(), skeleton->boneCount(), kind.boxWidth, kind.boxLength, kind.boxHeight,
		kind.shadowRadius);
	const CreatureClipMotion &clipMotion = def.clipMotion;
	if(clipMotion.path.empty()){
		setupPmxBones(kind, *skeleton);
		return true;
	}
	// クリップの長さの単位は、モデルと同じ(glb のメートルなど)
	auto clips = std::make_shared<const std::vector<model::AnimationClip>>(model::loadAnimationClips(ResourcePaths::resource(clipMotion.path.c_str())));
	if(clips->empty()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy '%s': no animation clips in %s", name.c_str(), clipMotion.path.c_str());
		return false;
	}
	kind.creature = std::make_unique<CreatureClipAnimator>(clips, *skeleton, clipMotion, def.behavior.fastSpeed, def.behavior.slowSpeed, def.motionBlend, 1.0f);
	SDL_Log("enemy '%s': moved by %zu clips (%s)", name.c_str(), clips->size(), clipMotion.path.c_str());
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
		// モデルのデータは、ファイルから1回だけ読み、全部の個体で共有する(GPUのバッファは個体ごと)
		for(int i = 0; i < type.count; ++i){
			auto model = createVulkanModel(window, resources, type.model);
			if(!model){
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "enemy '%s': failed to create the model %s (%d of %d)", type.name.c_str(), type.model.c_str(), i,
					type.count);
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
			// クリップで動かす体: 早歩きか、ゆっくり歩きか、待機・攻撃の速さ・時計・最初に攻撃を試すまでの時間の個体差(全員が揃って動かないように)
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
		gridCell_ = std::max(gridCell_, 2.0f * std::hypot(0.5f * kind.boxWidth + std::fabs(kind.boxSide),
			0.5f * kind.boxLength + std::fabs(kind.boxForward) + std::max(type.box.fallLength, 0.0f)));
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

// 個体の当たりの箱: 向き(yaw)に合わせて回した箱(幅×長さ。中心は、メッシュの範囲の中心)を囲む、軸に平行な箱。攻撃Aで前へ倒れ込んでいる間は、倒れた度合いだけ前へ伸ばす。
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
	const float forwardX = std::sin(enemy.yaw), forwardZ = std::cos(enemy.yaw); // 体の正面の向き(左は (forwardZ, -forwardX))
	const float forward = kind.boxForward + 0.5f * extra;
	collider.offsetX = forward * forwardX + kind.boxSide * forwardZ;
	collider.offsetZ = forward * forwardZ - kind.boxSide * forwardX;
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
	const auto &j = kind.def.jump;
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
		// クリップで動かす体が攻撃している間は、攻撃の動きだけ(歩き・押し合い・向きの変化は止める)
		if(!kind.creature || !updateCreature(kind, enemy, dt, distance, dx, dz, map, rules, props)){
			const float baseSpeed = !kind.creature ? def.speed : (enemy.fast ? def.behavior.fastSpeed : def.behavior.slowSpeed);
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
				// クリップで動かす体: 歩いている間は、早歩きか、ゆっくり歩き。止まったら待機
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
	const auto &j = kind.def.jump;
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

// クリップで動かす体の攻撃。攻撃している間は true(跳ぶ攻撃なら、跳び立つ点から着地点(chooseLanding・planJump)へ、位置を放物線で動かす)。
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
			enemy.posedStill = !kind.creature && enemy.walkWeight <= kStillWeight; // クリップで動かす体は、止まっても動き続ける
		}
		// 倒れ込み・着地の潰れ(地面 = 休止ポーズのメッシュのいちばん低い点 groundY を基準に、高さを縮めて前後・左右へ広げる。地面にめり込まず、浮かない)
		const CreatureDriver::BodyScale squash = kind.creature ? kind.creature->bodyScale(enemy.creature) : CreatureDriver::BodyScale{};
		transform_.setPos(geo::Vector3f(enemy.x, enemy.y - kind.groundY * kind.scale * squash.vertical, enemy.z));
		transform_.setRotation(geo::Quaternionf::createRotater(enemy.yaw, geo::Vector3f(0.0f, 1.0f, 0.0f)));
		transform_.setScale(geo::Vector3f(kind.scale * squash.horizontal, kind.scale * squash.vertical, kind.scale * squash.horizontal));
		window.draw(enemy.model, viewProj, transform_.getMatrix());
		shadow.draw(window, viewProj, enemy.x, enemy.y, enemy.z, kind.shadowRadius, kind.def.shadowOpacity);
	}
}

} // namespace game
