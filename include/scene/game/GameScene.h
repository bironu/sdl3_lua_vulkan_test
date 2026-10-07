#if !defined(GAME_GAMESCENE_H_)
#define GAME_GAMESCENE_H_

#include "geo/AffineMap.h"
#include "model/Vrma.h"
#include "scene/Scene.h"
#include "scene/game/GameSettings.h"
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
//   HUD(res/lua/ui/hud.lua。FPS・操作説明・ミニマップ): Luaのウィジェットで作る。H: 操作説明、M: ミニマップの表示切替、F5: スクリプト・調整値(res/lua/data/game_settings.lua)の読み直し
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
	// 調整値(速さ・カメラ・クロスフェードなど)は settings_(res/lua/data/game_settings.lua。F5で読み直す)。カメラの高さ・距離は、モデルの背の高さ(頂点の最大のY。applyModelHeight)に比例する
	static constexpr float kMinPitch = -1.5707963f; // 真上を向く

	// カメラの位置・注視点・上向きを、yaw/pitch/distanceから求める。地面に潜りそうなときは、体に沿って頭の上へ上がり、真上を向く(ダークソウル風)
	void applyModelHeight(float height); // プレイヤーのモデルの背の高さから、カメラの高さ・距離を決める(距離は初期値)
	void reloadSettings();               // 調整値(game_settings.lua)と、地形の設定(field_settings.lua)を読み直す(F5)
	// 地形(丘・坂)にめり込みそうなときは、頭からカメラへの線が地面に当たる手前まで、カメラを引き寄せる(dtは、離れていくときのなめらかさに使う)
	void computeCamera(float dt, geo::Vector3f &eye, geo::Vector3f &lookAt, geo::Vector3f &up);
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
	enum Motion { MotionIdle, MotionWalk, MotionSlowRun, MotionFastRun, MotionClimb, MotionRoll, MotionPunchRight, MotionPunchLeft, MotionKickHigh, MotionRoundhouse, MotionCrouchEnter, MotionCrouchExit, MotionCount };
	static bool isAction(int motion) { return motion >= MotionRoll; }
	void startAction(int motion, float dirX, float dirZ);
	// 向き(dirX, dirZ。単位ベクトル)へ、distanceメートル進む(縁・水・勾配・置物で止まる)
	// maxSlopeが0以上なら、地面の設定の勾配の上限の代わりにそれを使う(急な坂をゆっくり登るとき)
	// 動いたらtrue
	bool stepMove(float dirX, float dirZ, float distance, float maxSlope = -1.0f);
	std::unique_ptr<model::VrmaPlayer> motions_[MotionCount];
	// クロスフェード: モーションが切り替わったら、切り替わる直前の姿勢から、新しいモーションの姿勢へ、短い時間でなめらかに混ぜる
	struct Pose
	{
		std::vector<model::Quat> rotations;
		std::vector<model::Vec3> translations;
	};
	static void capturePose(const model::Skeleton &skeleton, Pose &pose);
	Pose lastPose_;          // 前のフレームの姿勢(混ぜる元)
	Pose fadeFrom_;          // フェード開始時の姿勢
	int appliedMotion_ = -1; // 前のフレームに当てたモーション
	float fadeTime_ = 0.0f, fadeDuration_ = 0.0f; // fadeTime_ < fadeDuration_ の間、フェード中
	// 坂登り: 登り始めに「しゃがむ」アクション(MotionCrouchEnter)、登っている間は climbing_、登り終わりに「立ち上がる」アクション(MotionCrouchExit)
	bool climbing_ = false;
	float climbRelease_ = 0.0f;  // 登れる坂でなくなってからの時間(settings_.climb.exitHold で立ち上がる)
	float climbSlope_ = 0.0f;    // 登っている坂の勾配(傾きの目標。なめらかにならしたもの)
	float tilt_ = 0.0f;          // いまのキャラの傾き(ラジアン。坂を登るとき、坂に沿って後ろへ傾く)
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
	float startX_ = 0.0f, startZ_ = 0.0f; // 最初の位置(ポーズ画面の「最初の位置へ戻る」で戻る)
	float heading_ = 0.0f; // キャラの向き(Y軸まわり。+Zが0)
	bool walking_ = false;

	float cameraYaw_ = 0.0f;   // 注視点から見たカメラの水平角(0でプレイヤーの+Z側=キャラの正面側)
	float cameraPitch_ = 0.25f;
	float cameraArm_ = 1.0f; // 地形に遮られていないときを1とした、頭からカメラまでの距離の割合(遮られたら縮め、遮りが無くなったらなめらかに戻す)
	GameSettings settings_;
	float cameraDistance_ = 3.5f;
	// モデルの背の高さから決まる、カメラの高さ・距離(applyModelHeight)
	float modelHeight_ = 1.6f;
	float cameraHeight_ = 1.35f;
	float minEyeHeight_ = 0.3f;
	float headTop_ = 1.9f;
	float minCameraDistance_ = 1.2f;
	float maxCameraDistance_ = 8.0f;

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
	bool autoPauseDone_ = false, autoMouseDone_ = false, autoReloadDone_ = false; // 動作確認用の環境変数(VULKAN_AUTOPAUSE/AUTOMOUSE)の、実行済み
	uint32_t startTick_ = 0;
	float sensitivity_ = 1.0f; // 視点の回転の感度の倍率(ポーズ画面のオプション)
};

} // namespace game

#endif // GAME_GAMESCENE_H_
