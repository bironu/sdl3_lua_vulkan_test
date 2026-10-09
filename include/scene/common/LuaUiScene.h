#if !defined(COMMON_LUAUISCENE_H_)
#define COMMON_LUAUISCENE_H_

#include "scene/Scene.h"
#include "scene/common/DebugAutomation.h"
#include "ui/PadNavigator.h"
#include "ui/UiContext.h"
#include "ui/UiScript.h"
#include <memory>
#include <string>

namespace game
{

// Luaスクリプト(ウィジェットの木。ui/UiScript参照)で見た目と入力を制御する、2Dの画面のシーン。
// 毎フレーム: スクリプトのupdate() → 木の描画 → 画面の更新。F5キーで、スクリプトを読み直す(リコンパイル不要。位置の微調整用)。
// キー・マウスはスクリプトのonKey()/onMouse*()へ渡す。シーン遷移はスクリプトの scene.change("名前")(SceneRegistry)
class LuaUiScene : public Scene
{
public:
	explicit LuaUiScene(std::string scriptPath);
	~LuaUiScene() override;

	void dispatch(const SDL_Event &) override;
	void onCreate(uint32_t tick) override;
	bool onIdle(uint32_t tick) override;

protected:
	// 派生クラスの毎フレームの処理(スクリプトのupdate()の後、描画の前)
	virtual void onFrame(uint32_t) {}
	// スクリプトの game.command(名前, 値) を受ける(派生クラスが、シーンごとの命令を実装する)
	virtual void onCommand(const std::string &, double) {}
	ui::UiContext &uiContext() { return *ctx_; }
	// 名前のシーンへ遷移する(登録が無ければログに出して何もしない)
	void changeScene(const std::string &name);

private:
	std::string scriptPath_;
	std::unique_ptr<ui::UiContext> ctx_;
	std::unique_ptr<ui::UiScript> script_;
	ui::PadNavigator padNavigator_; // ゲームパッドの十字キー・左スティックを、上下左右のキーとして渡す
	bool reloadRequested_ = false;
	DebugAutomation automation_; // 動作確認用の環境変数(onCreateで読む)
	uint32_t startTick_ = 0;
	uint32_t lastTick_ = 0;
};

} // namespace game

#endif // COMMON_LUAUISCENE_H_
