#if !defined(VULKANSAMPLESCENE_H_)
#define VULKANSAMPLESCENE_H_

#include "scene/Scene.h"
#include "geo/AffineMap.h"
#include "vk/VulkanMaterial.h"
#include "model/Motion.h"
#include "model/Retarget.h"
#include "model/Vrma.h"
#include "vk/VulkanMesh.h"
#include "vk/VulkanModel.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

// VulkanWindow用の起動確認シーン。メッシュ(立方体)を共有しつつ、
// geo::AffineMap/Quaternionで個別に変換した複数オブジェクトを描画する:
//   中央で自転するPMXモデル + その周りを公転する立方体3つ + 色の違う3つの点光源(それぞれ目印の発光体付き)
// 立方体はそれぞれ別のテクスチャ・材質(光沢の強さ)を持つ。
// 平行光源と点光源(それぞれ)のシャドウマップで、モデルと立方体の影が床に落ちる
// ライティングは平行光源 + 点光源のBlinn-Phong(拡散+鏡面反射)
class VulkanSampleScene : public Scene
{
public:
	VulkanSampleScene();
	~VulkanSampleScene() override;

	void dispatch(const SDL_Event &) override;
	void onSuspend() override;
	void onCreate(uint32_t tick) override;
	bool onIdle(uint32_t tick) override;

private:
	void zoomCamera(float amount); // amount > 0で近づく
	void loadVrm(int index);
	// 全モデル表示(Gキー): res/model/の全ファイルを(各kAllCopies体ずつ)読み込んで格子状に並べ、同じモーションで踊らせる(負荷の確認用)。
	// 単独表示のモデル(vrmModel_)とは別に持つ。最初に表示するときに読み込む
	struct VrmActor
	{
		std::shared_ptr<VulkanModel> model;
		std::array<std::unique_ptr<model::VrmaPlayer>, 9> players; // kMotionCountと同じ数
		geo::AffineMap transform;
		uint32_t lastTick = 0;
		float lastFrame = 0.0f;
	};
	void loadAllStep(); // 読み込みを1体ぶん進める(毎フレーム。イベント処理を止めない)
	void updateActor(VrmActor &actor, uint32_t tick, float t);
	void updateWindowTitle();
	// 地面の丸い影(ブロブ): シャドウマップを使わないときの、足元の影の近似。floorYの高さの床に、(x, z)を中心とした、ぼかした円を描く。
	// heightは物の高さ(床から)で、高いほど薄く大きくする
	void drawBlobShadow(const geo::Matrix4x4f &viewProj, float x, float z, float radius, float opacity, float height = 0.0f);
	std::shared_ptr<VulkanTexture> blobTexture_; // 中心が黒く、縁へなめらかに透明になる円 // AA・カリング・モデル数を、ウィンドウのタイトルに出す
	std::vector<std::unique_ptr<VrmActor>> allActors_; // AffineMapが移動できないので、ポインタで持つ
	bool allMode_ = true;           // Gで全モデル表示/単独表示(Vで切り替える)を切り替える
	bool allLoaded_ = false;
	size_t allLoadPath_ = 0;  // 読み込み中のvrmPaths_の番号と、そのコピーの番号
	int allLoadCopy_ = 0;
	std::shared_ptr<const model::ModelData> allData_; // 読み込み中のモデルのデータ(コピー同士で共有)
	std::map<std::string, std::shared_ptr<VulkanTexture>> allPathTextures_;
	std::map<std::pair<const void *, bool>, std::shared_ptr<VulkanTexture>> allEmbeddedTextures_;
	uint32_t allStartTick_ = 0;     // 全モデルのモーションを始めたtick(全員そろえる。0なら次のフレームから)
	static constexpr float kAllCameraDistance = 15.0f;
	static constexpr float kAllCameraPitch = 0.5f;
	static constexpr int kAllCopies = 10;          // 全モデル表示で、同じモデルを並べる数
	static constexpr float kAllSpacingX = 1.1f;    // コピー同士の横の間隔(m)
	static constexpr float kAllSpacingZ = 1.4f;    // モデルの種類ごとの列の奥行きの間隔(m)
	static constexpr size_t kCubeCount = 3;

	std::shared_ptr<VulkanModel> aquaModel_; // PMXモデル(湊あくあ公式MMD)。再配布禁止なのでres/model/はリポジトリに含めない
	std::shared_ptr<VulkanMesh> cubeMesh_;
	std::shared_ptr<VulkanMesh> floorMesh_; // 影が落ちる床
	std::shared_ptr<VulkanMesh> lampMesh_; // 点光源の位置を示す自発光の小さな立方体(光源の数だけ描く)
	geo::AffineMap aquaTransform_;
	// VRMモデル(res/model/test2.vrm、無ければ最初のVRM)。Vキーで、PMXモデルと切り替える(初めて表示するときに読む)
	std::shared_ptr<VulkanModel> vrmModel_;
	geo::AffineMap vrmTransform_;
	bool showVrm_ = true;
	std::vector<std::string> vrmPaths_; // res/model/にあるVRMファイル(名前順)。Vキーで順に切り替える(MMDのモデルがあれば、最後にMMDも)
	bool vrmListed_ = false;
	int vrmIndex_ = -1;         // 表示中のVRMの番号(vrmPaths_の添字。MMDを表示中、または未選択なら-1)
	int pendingVrmSlot_ = -1;   // 次のフレームで切り替える先(-1なら無し。vrmPaths_.size()はMMD)
	// VRMAのモーション: 番号0=ダンス(res/motion/dance.vrma)、1〜7=res/motion/VRMA_01〜07.vrma、8=res/motion/Walking.vrma(MixamoのFBXをfbx2vrmaで変換したもの)。キーボードの0〜8で再生する
	// (0はダンスの再生/停止、1〜7はそのモーションを最初から再生。停止中は休止ポーズ(Tポーズ)でまばたきだけ)
	static constexpr int kMotionCount = 9;
	std::array<std::unique_ptr<model::VrmaPlayer>, kMotionCount> vrmaPlayers_;
	int activeMotion_ = 0;      // 再生中のモーションの番号。-1なら停止中
	uint32_t vrmaStartTick_ = 0; // 再生を始めたtick(0なら次のフレームから)
	void startMotion(int index);
	uint32_t lastVrmTick_ = 0; // VRMのスプリングボーンを前回進めたtick(0なら未開始)
	float lastVrmFrame_ = 0.0f; // 前回のモーションのフレーム番号(ループの検出用)
	std::unique_ptr<model::Retargeter> retargeter_; // MMDのモーション(aquaModel_に当てたもの)をVRMへ写す。作れなければnullptr(Tポーズのまま)
	// カメラはキャラ(注視点)を中心に回る: ドラッグで回転、ホイール/ピンチで拡大縮小
	static constexpr float kInitialCameraYaw = 0.0f;
	float cameraYaw_ = kInitialCameraYaw;   // 注視点の周りの水平角(ラジアン)。0でキャラの正面(+Z側)
	float cameraPitch_ = 0.175f;            // 仰角(ラジアン)。正で見下ろす
	float cameraDistance_ = 3.45f;          // 注視点からの距離
	// 注視点(カメラが回る中心)。W/A/S/Dキーで、前/左/後ろ/右へ水平に動かす。Rで、注視点・角度・距離を初期値に戻す
	static constexpr float kInitialCameraTarget[3] = {0.0f, -0.2f, 0.0f};
	static constexpr float kInitialCameraPitch = 0.175f;
	static constexpr float kInitialCameraDistance = 3.45f;
	float cameraTarget_[3] = {kInitialCameraTarget[0], kInitialCameraTarget[1], kInitialCameraTarget[2]};
	uint32_t lastCameraTick_ = 0;
	bool toonShading_ = false; // TABで切り替え
	uint32_t motionStartTick_ = 0; // モーションの再生を始めたtick(最初のonIdle。モデルの読み込み時間を含めない)
	bool motionStarted_ = false;
	uint32_t lastPhysicsTick_ = 0; // 物理演算を前回進めたtick(0なら未開始)
	float lastMotionFrame_ = 0.0f; // 前回のモーションのフレーム番号(ループの検出用)
	std::unique_ptr<model::MotionPlayer> aquaMotion_; // モデルに当てるモーション(VMD)。無ければ手動のテストポーズ
	static constexpr size_t kLampCount = 3;
	// Spaceキーで一時停止: アニメーション(モーション・物理演算・ランプや立方体の動き)を止める。カメラ操作は止めない(気になる場面のキャプチャ用)。
	// 止めている間の時間は、シーンの時刻から引く(再開したとき、モーションが飛ばない)
	bool cameraLight_ = false; // 光の向きをカメラ追従にするか(Hキー)
	bool paused_ = false;
	uint32_t pausedAt_ = 0;    // 止めたときのSDL_GetTicks()
	uint32_t pausedTotal_ = 0; // これまで止めていた時間の合計(ms)
	size_t activeLamps_ = 0; // 点灯している点光源の数(0〜kLampCount)。Lキーで変える
	std::array<geo::AffineMap, kLampCount> lampModels_;
	geo::AffineMap floorModel_;
	std::array<geo::AffineMap, kCubeCount> cubeModels_;
	// オブジェクトごとの材質(テクスチャ・鏡面反射)。十字と立方体3つで全部違う
	VulkanMaterial floorMaterial_;
	std::array<VulkanMaterial, kLampCount> lampMaterials_; // ランプ(自発光): 光源の番号の色で光る。影は落とさず、受けない
	std::array<VulkanMaterial, kCubeCount> cubeMaterials_;
};

#endif // VULKANSAMPLESCENE_H_
