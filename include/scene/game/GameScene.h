#if !defined(GAME_GAMESCENE_H_)
#define GAME_GAMESCENE_H_

#include "geo/AffineMap.h"
#include "model/Vrma.h"
#include "scene/Scene.h"
#include "field/FieldMap.h"
#include "field/FieldMovement.h"
#include "scene/common/BlobShadow.h"
#include "scene/common/FieldRenderer.h"
#include "scene/common/PropRenderer.h"
#include "vk/VulkanMaterial.h"
#include "vk/VulkanMesh.h"
#include "vk/VulkanModel.h"
#include "ui/PadNavigator.h"
#include "ui/UiContext.h"
#include "ui/UiScript.h"
#include <memory>

namespace game
{

// ゲーム本体(最初の段階): 地面のタイルのフィールド(res/lua/data/field_settings.lua で指定したファイル。フィールドエディタで作る)を、プレイヤー(キャラクタ選択画面で選んだキャラ。GameSession)が歩く。
//   W/A/S/D: カメラから見た前/左/後ろ/右へ移動(キャラは進む向きを向く)。壁に当たると、それ以上進めない
//   ゲームパッド: 左スティックで移動(傾きの分だけ進む)、右スティックで視点の回転
//   マウス: 視点の回転(三人称視点。マウスはウィンドウに取り込む)、ホイール: カメラの距離。Esc/Start: ポーズ(res/lua/ui/pause.lua。再開・言語・感度・タイトルへ・終了)
//   HUD(res/lua/ui/hud.lua。FPS・操作説明・ミニマップ): Luaのウィジェットで作る。H: 操作説明、M: ミニマップの表示切替、F5: スクリプトの読み直し
class GameScene : public Scene
{
public:
	// GameSceneが使うデータの読み込み一覧(Lua)。LoadingSceneが先に読んでおく
	static constexpr const char *kAssetManifest = "res/lua/game.assets.lua";
	static constexpr const char *kHudScript = "res/lua/ui/hud.lua";
	static constexpr const char *kPauseScript = "res/lua/ui/pause.lua";

	GameScene();
	~GameScene() override;

	void dispatch(const SDL_Event &) override;
	void onSuspend() override {}
	void onCreate(uint32_t tick) override;
	void onDestroy(uint32_t tick) override;
	bool onIdle(uint32_t tick) override;

private:
	// フィールド(地面のタイル): 原点の角から、x・z の正の向きへ広がる。縁には壁は無いが、外へは出られない(当たり判定)
	static constexpr float kPlayerRadius = 0.3f;  // 壁との当たりの半径
	static constexpr float kWalkSpeed = 1.6f;     // m/秒(歩きのモーションの足運びに合わせた値)
	static constexpr float kSlowRunSpeed = 3.1f;  // m/秒(Slow Runのモーションの足運びに合わせた値。モーションが進む距離2.29m ÷ 0.73秒)
	static constexpr float kFastRunSpeed = 5.7f;  // m/秒(Fast Run: 3.02m ÷ 0.53秒)
	static constexpr float kRollDistance = 4.5f;  // Stand To Rollで前へ転がる距離(モーションが進む距離。モーションの長さの間に、等速で進む)
	static constexpr float kRunStick = 0.95f;     // 左スティックをこれ以上傾けると「最大」(走る)
	static constexpr float kTapTime = 0.25f;      // Aボタンを、これより短く押して離したら単押し(転がる)。これ以上押し続けたら長押し(全力で走る)
	static constexpr float kTriggerOn = 0.5f;     // L2/R2を押したとみなすトリガーの値
	static constexpr float kTurnSpeed = 12.0f;    // キャラが進む向きへ向く速さ(ラジアン/秒の目安)
	// カメラの高さ・距離は、モデルの背の高さ(頂点の最大のY。applyModelHeight)に比例する。下は、背の高さ1.6mのモデルを基準にした比率
	static constexpr float kReferenceHeight = 1.6f;
	static constexpr float kCameraHeightRatio = 1.35f / kReferenceHeight;   // 注視点(プレイヤーの頭のあたり)の高さ
	static constexpr float kMinEyeHeightRatio = 0.3f / kReferenceHeight;    // カメラの、地面からの最低の高さ。これより下へ行きそうなら、体に沿って頭の上へ回る
	static constexpr float kHeadTopRatio = 1.9f / kReferenceHeight;         // 体に沿って上がったカメラが、最後に着く高さ(頭のてっぺんの上)
	static constexpr float kDefaultDistanceRatio = 3.5f / kReferenceHeight; // カメラの距離の初期値
	static constexpr float kMinPitch = -1.5707963f; // 真上を向く
	static constexpr float kMinDistanceRatio = 0.75f;
	static constexpr float kMaxDistanceRatio = 5.0f;

	// カメラの位置・注視点・上向きを、yaw/pitch/distanceから求める。地面に潜りそうなときは、体に沿って頭の上へ上がり、真上を向く(ダークソウル風)
	void applyModelHeight(float height); // プレイヤーのモデルの背の高さから、カメラの高さ・距離を決める(距離は初期値)
	void computeCamera(geo::Vector3f &eye, geo::Vector3f &lookAt, geo::Vector3f &up) const;
	float fieldWidth() const { return map_.width() * map_.cellSize(); }
	float fieldDepth() const { return map_.depth() * map_.cellSize(); }
	void updatePlayer(float dt, uint32_t tick);
	void drawScene(const geo::Matrix4x4f &viewProj);
	void updateHud(float dt, uint32_t tick);
	void setPaused(bool paused);
	void command(const std::string &name, double value); // ポーズ画面のスクリプトからの命令(game.command)
	void toScreen(float windowX, float windowY, float &x, float &y);

	std::shared_ptr<VulkanModel> player_;
	// モーション(キャラへ当てるVRMA)。立ち・歩き・走りは、動きに合わせてループする。転がる・攻撃は、1回だけ再生する「アクション」で、終わるまで他の操作を受けない
	enum Motion { MotionIdle, MotionWalk, MotionSlowRun, MotionFastRun, MotionRoll, MotionPunchRight, MotionPunchLeft, MotionMartelo, MotionRoundhouse, MotionCount };
	static bool isAction(int motion) { return motion >= MotionRoll; }
	void startAction(int motion, float dirX, float dirZ);
	// 向き(dirX, dirZ。単位ベクトル)へ、distanceメートル進む(縁・水・勾配・置物で止まる)
	void stepMove(float dirX, float dirZ, float distance);
	std::unique_ptr<model::VrmaPlayer> motions_[MotionCount];
	int motion_ = MotionIdle;
	float motionTime_ = 0.0f;
	bool actionHeld_ = false;    // Aボタン(キーボードはSpace)が押されている
	float actionHeldTime_ = 0.0f;
	bool actionPrev_[4] = {false, false, false, false}; // R1・R2・L1・L2(キーボードは X V Z C)が、前のフレームで押されていたか
	float rollDirX_ = 0.0f, rollDirZ_ = 1.0f;
	int pendingAuto_ = -1;      // 動作確認用: 押されたことにするアクション
	size_t autoActionDone_ = 0; // 動作確認用の環境変数(VULKAN_AUTOACTION)の、実行済みの数
	geo::AffineMap playerTransform_;
	float playerX_ = 0.0f, playerZ_ = 0.0f;
	float heading_ = 0.0f; // キャラの向き(Y軸まわり。+Zが0)
	bool walking_ = false;

	float cameraYaw_ = 0.0f;   // 注視点から見たカメラの水平角(0でプレイヤーの+Z側=キャラの正面側)
	float cameraPitch_ = 0.25f;
	float cameraDistance_ = kDefaultDistanceRatio * kReferenceHeight;
	// モデルの背の高さから決まる、カメラの高さ・距離(applyModelHeight)
	float modelHeight_ = kReferenceHeight;
	float cameraHeight_ = kCameraHeightRatio * kReferenceHeight;
	float minEyeHeight_ = kMinEyeHeightRatio * kReferenceHeight;
	float headTop_ = kHeadTopRatio * kReferenceHeight;
	float minCameraDistance_ = kMinDistanceRatio * kReferenceHeight;
	float maxCameraDistance_ = kMaxDistanceRatio * kReferenceHeight;

	// 地面
	std::vector<field::TileDef> tiles_;
	field::FieldMap map_;
	std::unique_ptr<FieldRenderer> fieldRenderer_;
	std::unique_ptr<PropRenderer> propRenderer_; // 置物(フィールドのファイルが持つ)
	field::MovementRules movementRules_;         // 歩けないタイル・勾配の上限
	field::PropCollision propCollision_;         // 置物の足元の当たり
	float playerY_ = 0.0f; // プレイヤーの足元の高さ(地面の高さ)
	std::unique_ptr<BlobShadow> blob_;
	uint32_t lastTick_ = 0;

	// HUD(Luaのウィジェット。3Dの上に重ねて描く)
	std::unique_ptr<ui::UiContext> uiContext_;
	std::unique_ptr<ui::UiScript> hud_;
	float fps_ = 0.0f;
	bool reloadHud_ = false;
	// ポーズ(Escで開閉)。開いている間は、ゲームを止めて、ポーズ画面(Luaのウィジェット)に、キー・マウス・ゲームパッドを渡す
	std::unique_ptr<ui::UiScript> pauseMenu_;
	ui::PadNavigator padNavigator_;
	bool paused_ = false;
	bool autoPauseDone_ = false, autoMouseDone_ = false; // 動作確認用の環境変数(VULKAN_AUTOPAUSE/AUTOMOUSE)の、実行済み
	uint32_t startTick_ = 0;
	float sensitivity_ = 1.0f; // 視点の回転の感度の倍率(ポーズ画面のオプション)
};

} // namespace game

#endif // GAME_GAMESCENE_H_
