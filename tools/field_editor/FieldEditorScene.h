#if !defined(FIELD_EDITOR_SCENE_H_)
#define FIELD_EDITOR_SCENE_H_

#include "field/FieldMap.h"
#include "geo/AffineMap.h"
#include "scene/Scene.h"
#include "scene/common/FieldRenderer.h"
#include "scene/common/PropRenderer.h"
#include "ui/PadNavigator.h"
#include "ui/UiContext.h"
#include "ui/UiScript.h"
#include "vk/VulkanMaterial.h"
#include "vk/VulkanMesh.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// フィールドエディタ(別実行ファイル field_editor): 地面のタイル・高さ・置物(ビル・岩・瓦礫・木など)を作る。
//   モード(T/H/O): タイル / 高さ / 置物
//   共通  右ドラッグ: 視点の回転  ホイール: ズーム  W/A/S/D: 視点の移動  Q/E: 回転  [ ]: ブラシの半径  Z: 元に戻す  Esc: メニュー(保存など)
//   タイル  左クリック/ドラッグ: 選んだタイルを塗る  1〜9: タイルの選択
//   高さ    左ドラッグ: 道具で地面を変える(Shiftで上げる/下げるが逆)  1〜4: 道具(上げる・下げる・ならす・平らにする)
//   置物    左クリック: 置く  X/Delete: カーソルの下の物を消す  1〜0: 一覧から選ぶ(PageUp/PageDown: ページ)  , .: 向き  - =: 大きさ  R/F: 持ち上げ
//   ファイルをウィンドウへドロップ(.glb/.vrm): res/prop/ へコピーして、一覧に足す(次からは、ファイルを指定しなくても一覧から使える)
// 保存先は res/lua/data/field_settings.lua の field(バイナリ)。画面の部品は、Luaのウィジェット(res/lua/ui/field_editor.lua)。タイルの定義はゲームと共通
class FieldEditorScene : public Scene
{
public:
	static constexpr const char *kUiScript = "res/lua/ui/field_editor.lua";
	static constexpr int kPropsPerPage = 10;
	enum Mode { ModeTile = 0, ModeHeight = 1, ModeProp = 2 };
	enum Tool { ToolRaise = 0, ToolLower = 1, ToolSmooth = 2, ToolFlatten = 3, ToolRamp = 4, ToolCount = 5 };

	FieldEditorScene();
	~FieldEditorScene() override;

	void dispatch(const SDL_Event &) override;
	void onCreate(uint32_t tick) override;
	bool onIdle(uint32_t tick) override;

private:
	// 元に戻す1回分(1ストロークか、1回の操作)
	struct Action
	{
		struct TileEdit { int index; uint8_t before; };
		struct HeightEdit { int index; int16_t before; };
		struct PropOp { bool added; size_t index; field::PlacedProp prop; };
		std::vector<TileEdit> tiles;
		std::vector<HeightEdit> heights;
		std::vector<PropOp> props;
		bool empty() const { return tiles.empty() && heights.empty() && props.empty(); }
	};

	void command(const std::string &name, double value);
	void toScreen(float windowX, float windowY, float &x, float &y);
	// ウィンドウの座標のマウスが指す地面の点(光線と地面の交点)。当たらなければfalse
	bool pickGround(float windowX, float windowY, float hit[3]) const;
	void paintTiles(float x, float z);
	void sculpt(float x, float z, float dt);
	void placeProp(float x, float z);
	void removeHoveredProp();
	void updateHoverProp();
	void beginStroke();
	void endStroke();
	void commit(Action &&action);
	void undo();
	void save();
	void reload();
	void rebuildField();
	void refreshProps();
	void importFile(const std::string &path);
	std::string filePath() const;
	void drawField(const geo::Matrix4x4f &viewProj);
	void rebuildSlopeMesh();   // 勾配の表示(Vキー)のメッシュを、いまの地形から作る
	void applyRamp();          // 斜面ツール: 始点から終点へ、一定の勾配でつなぐ
	void drawRampPreview(const geo::Matrix4x4f &viewProj);
	// 斜面の分類(0: 歩ける、1: 歩いては登れないがゆっくり登れる、2: 進めない)
	int slopeClass(float slope) const;
	std::string selectedProp() const;
	void runAuto(const std::string &spec);

	field::FieldSettings settings_;
	std::vector<field::TileDef> tiles_;
	field::FieldMap map_;
	std::unique_ptr<game::FieldRenderer> renderer_;
	std::unique_ptr<game::PropRenderer> propRenderer_;
	std::vector<std::string> propList_;
	std::shared_ptr<VulkanTexture> white_;

	std::unique_ptr<ui::UiContext> uiContext_;
	std::unique_ptr<ui::UiScript> ui_;
	ui::PadNavigator padNavigator_;

	// カメラ
	float targetX_ = 0.0f, targetY_ = 0.0f, targetZ_ = 0.0f;
	float yaw_ = 0.0f, pitch_ = 0.95f, distance_ = 26.0f;
	geo::Vector3f eye_;
	float fovY_ = 1.0471976f; // 60度

	// 編集の状態
	int mode_ = ModeTile;
	int brush_ = 0;       // タイル
	int tool_ = ToolRaise;
	int radius_ = 1;      // タイル・高さのブラシの半径(マス)
	int propSel_ = -1;    // propList_の番号
	int propPage_ = 0;
	float propYaw_ = 0.0f, propScale_ = 1.0f, propLift_ = 0.0f;
	bool hasHit_ = false;
	float hit_[3] = {0.0f, 0.0f, 0.0f};
	int hoverProp_ = -1;  // カーソルの下の置物(map_.props()の番号)
	bool painting_ = false, rotating_ = false;
	float mouseX_ = 0.0f, mouseY_ = 0.0f;
	float flattenHeight_ = 0.0f;
	Action stroke_;
	std::unordered_set<int> touchedHeights_;
	std::vector<Action> undoStack_;
	int savedCount_ = 0, importedCount_ = 0;
	bool dirty_ = false;
	bool showSlopes_ = false;    // 地面の勾配を色で表示する(Vキー): 緑=歩ける、黄=ゆっくり登れる、赤=進めない、紫=歩けないタイル
	bool slopeDirty_ = true;     // 勾配のメッシュを作り直す必要がある(地形を変えた)
	std::shared_ptr<VulkanMesh> slopeMesh_;
	VulkanMaterial slopeMaterial_;
	bool rampActive_ = false;    // 斜面ツールで、始点を決めてドラッグ中
	float rampStart_[2] = {0.0f, 0.0f}, rampEnd_[2] = {0.0f, 0.0f}; // 始点・終点(ワールドのx, z)
	bool showCollision_ = false; // 置物の足元の当たりの長方形を表示する(Cキー)
	uint32_t lastTick_ = 0;
	uint32_t startTick_ = 0;
	size_t autoDone_ = 0;
	size_t autoKeyDone_ = 0;
};

#endif // FIELD_EDITOR_SCENE_H_
