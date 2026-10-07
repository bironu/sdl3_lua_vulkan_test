#if !defined(FIELD_EDITOR_SCENE_H_)
#define FIELD_EDITOR_SCENE_H_

#include "field/FieldMap.h"
#include "geo/AffineMap.h"
#include "scene/Scene.h"
#include "ui/PadNavigator.h"
#include "ui/UiContext.h"
#include "ui/UiScript.h"
#include "vk/VulkanMaterial.h"
#include "vk/VulkanMesh.h"
#include <memory>
#include <vector>

// フィールドエディタ(別実行ファイル field_editor): 地面のタイルを、格子の上に並べる。
//   左クリック/ドラッグ: 選んだタイルを置く(画面の部品の上は除く)  右ドラッグ: 視点の回転  ホイール: ズーム  W/A/S/D: 視点の移動  Q/E: 回転
//   1〜9: タイルの選択  Z: 元に戻す  Esc: メニュー(再開・保存・読み直し・全面を塗る・終了。保存先は res/lua/data/field_settings.lua の field)
// 画面の部品(パレット・情報)は、Luaのウィジェット(res/lua/ui/field_editor.lua)。タイルの定義はゲームと共通(res/lua/data/field_tiles.lua)
class FieldEditorScene : public Scene
{
public:
	static constexpr const char *kUiScript = "res/lua/ui/field_editor.lua";

	FieldEditorScene();
	~FieldEditorScene() override;

	void dispatch(const SDL_Event &) override;
	void onSuspend() override {}
	void onCreate(uint32_t tick) override;
	bool onIdle(uint32_t tick) override;

private:
	struct Edit
	{
		int x, z;
		uint8_t before;
	};

	void command(const std::string &name, double value);
	void toScreen(float windowX, float windowY, float &x, float &y);
	// ウィンドウの座標のマウスが指す地面のマス(ワールドのy=0の面との交点)。範囲外ならfalse
	bool pickCell(float windowX, float windowY, int &cellX, int &cellZ) const;
	void paint(int cellX, int cellZ);
	void endStroke();
	void undo();
	void save();
	void reload();
	std::string filePath() const;
	void drawField(const geo::Matrix4x4f &viewProj);

	field::FieldSettings settings_;
	std::vector<field::TileDef> tiles_;
	field::FieldMap map_;
	std::vector<VulkanMaterial> tileMaterials_;
	VulkanMaterial baseMaterial_;
	std::shared_ptr<VulkanMesh> quad_;
	std::shared_ptr<VulkanTexture> white_;

	std::unique_ptr<ui::UiContext> uiContext_;
	std::unique_ptr<ui::UiScript> ui_;
	ui::PadNavigator padNavigator_;

	// カメラ
	float targetX_ = 0.0f, targetZ_ = 0.0f;
	float yaw_ = 0.0f, pitch_ = 0.95f, distance_ = 26.0f;
	geo::Vector3f eye_;
	float fovY_ = 1.0471976f; // 60度

	int brush_ = 0;
	int hoverX_ = -1, hoverZ_ = -1;
	bool painting_ = false, rotating_ = false;
	float mouseX_ = 0.0f, mouseY_ = 0.0f;
	std::vector<Edit> stroke_;
	std::vector<std::vector<Edit>> undoStack_;
	int savedCount_ = 0;
	bool dirty_ = false;
	uint32_t lastTick_ = 0;
	uint32_t startTick_ = 0;
	bool autoDone_ = false;
	size_t autoKeyDone_ = 0;
};

#endif // FIELD_EDITOR_SCENE_H_
