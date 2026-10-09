#if !defined(GAME_ENEMYHORDE_H_)
#define GAME_ENEMYHORDE_H_

#include "geo/AffineMap.h"
#include "geo/Matrix.h"
#include "model/CreatureBuilder.h"
#include "scene/game/CreatureAnimator.h"
#include "vk/VulkanModel.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

class ResourceSet;
namespace SDL_
{
class VulkanWindow;
}
namespace field
{
class FieldMap;
struct MovementRules;
}

namespace game
{

class BlobShadow;

// 敵の種類の定義。res/lua/data/enemies.lua の表から読む(読めない項目は、ここの既定のまま)。長さはメートル、時間は秒、角度はラジアン。
// 体は2通り: creature(手続き的に作る丸い生き物の体。動きは CreatureAnimator)か、model(PMXのファイル。動きはMMDの名前のボーンを回す歩き)
struct EnemyType
{
	std::string name;
	std::optional<model::CreatureSpec> creature; // 丸い生き物の体の形(あれば、model より優先)
	std::string model;            // モデルのパス(PMX。ボーンはMMDの標準の名前)
	int count = 0;
	uint32_t seed = 1;            // 出す位置・個体差の乱数の種
	float height = 1.2f;          // 背の高さ(モデルの頂点の最大の高さを、これに合わせて拡大縮小する)
	float length = 0.0f;          // 体長(モデルの前後の長さを、これに合わせる。0なら height で合わせる)
	float speed = 1.4f;           // 歩く速さ(PMXの体。丸い生き物の体は、動きの表 walkFast / walkSlow の speed)
	float speedJitter = 0.25f;    // 速さの個体差(±割合)
	float turnSpeed = 5.0f;
	float spawnMinRadius = 6.0f;  // プレイヤーからこの距離〜spawnRadiusの輪の中に出す
	float spawnRadius = 20.0f;
	float stopDistance = 2.0f;    // プレイヤーにこの距離まで近づいたら止まる
	float stopJitter = 3.0f;      // 止まる距離の個体差(0〜この値を足す)
	// 敵同士の当たりの箱(軸に平行な箱。向きに合わせて回した箱を囲む。XZの平面で重なりを押し出し、高さの範囲も持つ)。
	// 幅・長さ・高さが0なら、モデルの寸法(全部の頂点を囲む大きさ)から
	struct Box
	{
		float width = 0.0f;      // 左右の幅(m)
		float length = 0.0f;     // 前後の長さ(m)
		float height = 0.0f;     // 高さ(m。足元から)
		float fallLength = 0.0f; // 攻撃Aで前へ倒れ込んだときに、前へ伸ばす長さ(m)
	} box;
	float cameraCullRadius = 1.5f; // カメラ(目の位置)から箱までが、この距離より近い敵は描かない(画面を覆わないよう。影も)
	float shadowRadius = 0.35f;
	float shadowOpacity = 0.5f;
	CreatureMotion creatureMotion; // 丸い生き物の体の動き(表の walkFast / walkSlow / idle / attackStand / attackJump / death)
	// 丸い生き物の体の行動: 歩いて近づき、プレイヤーとの距離に応じて、攻撃を選ぶ(当たり判定は無く、見た目の動きだけ)
	struct Behavior
	{
		float fastRatio = 0.5f;   // 早歩きで歩く個体の割合(残りは、ゆっくり歩き)
		float closeRange = 1.8f;  // プレイヤーまでの距離がこれ以下なら、攻撃A(立ち上がって倒れ込む)を始められる
		float jumpMin = 2.0f;     // 距離がこれより遠く、jumpRange 以下なら、攻撃B(跳びかかる)を始められる
		float jumpRange = 4.0f;
		float standChance = 0.6f; // 攻撃を試す時期に、攻撃Aを始める確率
		float jumpChance = 0.5f;  // 同、攻撃B
		float cooldown = 2.5f;    // 攻撃の後、次の攻撃を試すまでの時間
		float cooldownJitter = 2.0f; // 同、個体差(0〜この値を足す)
		float retry = 0.6f;       // 攻撃を試して始めなかったとき、次に試すまでの時間
		float timeJitter = 0.15f; // 待機・攻撃の速さの個体差(±割合)
	} behavior;
	// PMXの歩き(手続き的なアニメーション。表の gait)
	struct Gait
	{
		float stride = 1.3f;      // 1周期(左右1歩ずつ)で進む距離
		float legSwing = 0.45f;   // 脚を前後に振る角度
		float kneeBend = 0.8f;    // 脚を前へ振り出すときに、膝を曲げる角度
		float ankle = 0.6f;       // 足首で、脚の傾きを打ち消す割合
		float armSwing = 0.45f;   // 腕を前後に振る角度
		float armDown = 0.35f;    // Aポーズの腕を、体の横へ下ろす角度
		float elbowBend = 0.35f;  // 肘を曲げておく角度
		float twist = 0.12f;      // 上半身のひねり
		float lean = 0.1f;        // 上半身の前傾
		float bob = 0.015f;       // 体の上下の揺れ(m)
		float sway = 0.015f;      // 体の左右の揺れ(m)
		float blend = 4.0f;       // 歩き出し・止まるときに、振れ幅が変わる速さ(1/秒)
	} gait;
	// アニメーションの間引き: プレイヤーから near までは毎フレーム、far で interval 秒ごと
	struct Lod
	{
		float near = 8.0f;
		float far = 30.0f;
		float interval = 0.2f;
	} lod;
	// PMXのボーンの名前(MMDの標準の名前)
	struct Bones
	{
		std::string center = "センター", upper = "上半身", lower = "下半身";
		std::string rightLeg = "右足", leftLeg = "左足", rightKnee = "右ひざ", leftKnee = "左ひざ", rightAnkle = "右足首", leftAnkle = "左足首";
		std::string rightArm = "右腕", leftArm = "左腕", rightElbow = "右ひじ", leftElbow = "左ひじ";
	} bones;
};

// 敵の定義の表を読む(パスはリポジトリ直下からの相対。読めなければ空。失敗はログに出す)
std::vector<EnemyType> loadEnemyTypes(const std::string &relativePath = "res/lua/data/enemies.lua");

// 敵のモデルの大きさ: 頂点の寸法(モデルの単位)と、モデルの単位→メートルの倍率(体長 length か、背の高さ height に合わせる)
struct EnemySize
{
	float top = 0.0f;    // 頂点のいちばん高い所
	float length = 0.0f; // 前後の長さ(前(-Z)・後ろ(+Z)の端の間。原点を含む)
	float side = 0.0f;   // 左右の、中心からいちばん遠い所
	float scale = 1.0f;
};
EnemySize measureEnemy(const EnemyType &type, const std::vector<model::ModelVertex> &vertices);

// 敵の大軍。個体ごとに位置・向き・歩きの位相・速さの個体差を持ち、毎フレーム、プレイヤーへ向かって歩く(近くで止まる)。
// 敵同士は、個体ごとの箱(EnemyType::Box)が重ならないよう押し出す(跳んでいる間は、他の敵の上を越える)。プレイヤー・ビルとの当たり・戦闘は無し。アニメーションは手続き的(IKは使わず、FKだけ): 丸い生き物の体は CreatureAnimator(早歩きか、ゆっくり歩きで近づき、
// 距離に応じて、立ち上がって倒れ込む攻撃か、跳びかかる攻撃をする。跳ぶ間は、位置を放物線で動かす)、
// PMXは、MMDの名前のボーンを周期的に回す歩き。
// 描画は、個体ごとにVulkanModel(GPUスキニング)を1つずつ持つ(メッシュのGPUバッファは個体の数だけ複製される)。遠い個体は、姿勢の更新を間引く
class EnemyHorde
{
public:
	// types の敵を、(playerX, playerZ) のまわりに出す。モデルが作れなかった種類は出さない(ログに出す)
	EnemyHorde(SDL_::VulkanWindow &window, ResourceSet &resources, const std::vector<EnemyType> &types, const field::FieldMap &map,
		float playerX, float playerZ);
	~EnemyHorde();

	// (targetX, targetZ)(プレイヤー)へ向かって歩かせる(姿勢を当て直す時期も決める)
	void update(float dt, const field::FieldMap &map, const field::MovementRules &rules, float targetX, float targetZ);
	// 画面に見えている敵に、(時期なら)姿勢を当ててから、敵と足元の丸い影を描く(描画の予約)。見えない敵は、姿勢の計算も描画も省く
	// eye はカメラの目の位置(近すぎる敵は描かない)
	void draw(SDL_::VulkanWindow &window, const geo::Matrix4x4f &viewProj, const BlobShadow &shadow, const geo::Vector3f &eye);
	size_t size() const { return enemies_.size(); }

private:
	// 種類ごとの、モデルから決まる値(ボーンの番号・大きさ)
	struct Kind
	{
		EnemyType def;
		float scale = 1.0f;     // モデルの単位→メートル
		float boxWidth = 1.0f, boxLength = 1.0f, boxHeight = 1.0f; // 当たりの箱(m。表の box か、モデルの寸法から)
		float boundY = 0.5f, boundRadius = 1.0f; // 画面に見えるかの判定の球(足元からの中心の高さと半径。m。手足の動きの分の余裕を含む)
		std::optional<CreatureAnimator> creature; // 丸い生き物の体の動き(PMXは無し)
		// 以下はPMXの歩き用
		float legLength = 0.0f; // 脚の付け根から足首までの長さ(モデルの単位)。脚を振ったときに腰を下げる分を求める
		int center = -1, upper = -1, lower = -1;
		int leg[2] = {-1, -1}, knee[2] = {-1, -1}, ankle[2] = {-1, -1}, arm[2] = {-1, -1}, elbow[2] = {-1, -1}; // [0]右、[1]左
		float armSide[2] = {1.0f, -1.0f}; // 腕を体の横へ下ろす、Z軸まわりの回転の向き(腕が -X 側へ伸びていれば +)
	};
	struct Enemy
	{
		size_t kind = 0;
		std::shared_ptr<VulkanModel> model;
		float x = 0.0f, y = 0.0f, z = 0.0f;
		float yaw = 0.0f;         // 向き(Y軸まわり。+Zが0。プレイヤーの heading と同じ)
		float phase = 0.0f;       // 歩きの位相(0〜1で1周期)
		float speedScale = 1.0f;  // 速さの個体差
		float stopDistance = 0.0f;
		float walkWeight = 0.0f;  // 歩きの振れ幅(0=止まっている、1=歩いている)
		float poseTimer = 0.0f;   // 前に姿勢を更新してからの時間(間引き用)
		bool moving = false;
		bool posedStill = false;  // 止まった姿勢(振れ幅0)を当て済み
		bool poseDue = false;     // 姿勢を当て直す時期(drawで、見えていれば当てる)
		CreatureAnimator::State creature; // 丸い生き物の体の動きの状態
		bool fast = false;        // 早歩きで歩く(でなければ、ゆっくり歩き)
		float cooldown = 0.0f;    // 次に攻撃を試すまでの時間
		float jumpFromX = 0.0f, jumpFromZ = 0.0f, jumpToX = 0.0f, jumpToZ = 0.0f; // 跳ぶ攻撃の、跳び立つ点と着地点
		bool pinned = false;      // 前のフレームで、重なりの押し出しを地形に止められた(壁際・崖際。押し出されにくくする)
		bool jumping = false;     // 跳ぶ攻撃の、着地するまで(位置を、跳び立つ点から着地点へ動かす間)
	};

	// 個体の当たりの箱(軸に平行)。中心は個体の位置から (offsetX, offsetZ)、半分の大きさ halfX・halfZ、高さは bottom〜top。solid でなければ判定しない(跳んでいる間)。
	// mobility は、重なりを押し出すときの動きやすさ(押し出す量を、2体の mobility の比で分ける。歩き・待機・攻撃で変える)
	struct Collider
	{
		float offsetX = 0.0f, offsetZ = 0.0f, halfX = 0.5f, halfZ = 0.5f, bottom = 0.0f, top = 1.0f;
		float mobility = 1.0f;
		bool solid = true;
	};

	static bool setupKind(Kind &kind, VulkanModel &model);
	static void setupPmxBones(Kind &kind, model::Skeleton &skeleton);
	Collider colliderOf(const Enemy &enemy) const;
	void buildGrid();
	void resolveOverlaps(const field::FieldMap &map, const field::MovementRules &rules);
	float overlapAt(const Enemy &self, float x, float z) const;
	void chooseLanding(const Kind &kind, Enemy &enemy, float targetX, float targetZ, const field::FieldMap &map);
	bool updateCreature(const Kind &kind, Enemy &enemy, float dt, float distance, float dx, float dz, const field::FieldMap &map,
		const field::MovementRules &rules);
	void applyPose(const Kind &kind, Enemy &enemy) const;
	void applyGait(const Kind &kind, Enemy &enemy) const;

	std::vector<Kind> kinds_;
	std::vector<Enemy> enemies_;
	// 箱の重なりを調べる格子(個体を、セルごとに並べる。毎フレームの確保を避けて使い回す)
	std::vector<uint32_t> cellStart_, cellItems_, cellOf_;
	std::vector<Collider> colliders_;  // 個体ごとの箱(毎フレーム、向き・動きから作り直す)
	std::vector<float> fromX_, fromZ_; // 重なりを押し出す前の位置(押し出した先へ、地形に沿って動かし直す)
	float gridCell_ = 1.0f; // セルの大きさ(m。2体の箱が重なりうる、位置の間の距離の最大)。格子はフィールドの原点の角から
	int gridW_ = 0, gridD_ = 0;
	geo::AffineMap transform_; // 描画の行列を作る作業用
	std::mt19937 rng_{12345};  // 攻撃を始めるかの乱数
};

} // namespace game

#endif // GAME_ENEMYHORDE_H_
