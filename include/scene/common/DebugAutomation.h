#if !defined(COMMON_DEBUGAUTOMATION_H_)
#define COMMON_DEBUGAUTOMATION_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui
{
class UiScript;
}

namespace game
{

// "名前@ミリ秒,名前@ミリ秒,…" を分解する("@"の無い項目は捨てる。順序は書いた順)
std::vector<std::pair<std::string, int>> parseTimedList(const char *text);

// 動作確認用の環境変数(VULKAN_AUTO*)で、入力・操作を自動で起こす。シーンが onCreate で fromEnv() して、メンバに持つ。
// 環境変数が無いものは、何も起きない(判定は、空・未設定かどうかを見るだけ)。時刻は、シーン側が決めた起点からの経過ミリ秒
//   VULKAN_AUTOKEY="キー名@ミリ秒,…"      その時間後に、キーが押されたことにする(LuaUiScene)
//   VULKAN_AUTOACTION="名前@ミリ秒,…"      その時間後に、アクションのボタンが押されたことにする(GameScene。名前の意味はシーン側)
//   VULKAN_AUTOPAUSE=ミリ秒 / VULKAN_AUTORELOAD=ミリ秒   その時間後に、1回だけ(ポーズ/F5の読み直し)
//   VULKAN_AUTOMOUSE="x,y@ミリ秒[,click]"  その時間後に、論理画面の(x, y)へマウスが動いたことにする(,clickならクリックも)
//   VULKAN_AUTOWALK=1/2/3, VULKAN_AUTORUN=1/2  GameSceneの入力の上書き
//   VULKAN_START="x,z", VULKAN_PITCH, VULKAN_YAW  GameSceneの初期値
class DebugAutomation
{
public:
	struct Mouse
	{
		float x = 0.0f, y = 0.0f;
		int at = 0;
		bool click = false;
	};

	static DebugAutomation fromEnv();

	// 経過時間に達した、まだ返していないキー/アクションを、1つずつ返す(無ければnullptr)。返した文字列は、次の呼び出しまで有効
	const std::string *nextKey(int elapsedMs) { return nextDue(keys_, keyDone_, elapsedMs); }
	const std::string *nextAction(int elapsedMs) { return nextDue(actions_, actionDone_, elapsedMs); }
	// 1回だけtrueを返す(時間に達したとき)
	bool takePause(int elapsedMs) { return takeOnce(pauseAt_, pauseDone_, elapsedMs); }
	bool takeReload(int elapsedMs) { return takeOnce(reloadAt_, reloadDone_, elapsedMs); }
	// 1回だけ、マウスの位置・クリックを返す(時間に達したとき)
	bool takeMouse(int elapsedMs, Mouse &out);
	// マウスの移動(と、クリックなら左ボタンの押下・解放)をスクリプトへ渡す
	static void sendMouse(ui::UiScript &script, const Mouse &mouse);

	// GameSceneの入力の上書き(環境変数が無ければ何もしない)
	void overrideMove(float &forward, float &right) const;
	void overrideRun(bool &run) const { run = run || hasRun_; }
	bool holdAction() const { return hasRun_ && runMode_ == '2'; }

	const std::optional<std::pair<float, float>> &startPosition() const { return start_; }
	const std::optional<float> &pitch() const { return pitch_; }
	const std::optional<float> &yaw() const { return yaw_; }

private:
	static const std::string *nextDue(const std::vector<std::pair<std::string, int>> &list, size_t &done, int elapsedMs);
	static bool takeOnce(int at, bool &done, int elapsedMs);

	std::vector<std::pair<std::string, int>> keys_, actions_;
	size_t keyDone_ = 0, actionDone_ = 0;
	int pauseAt_ = -1, reloadAt_ = -1; // -1: 環境変数が無い
	bool pauseDone_ = false, reloadDone_ = false;
	std::optional<Mouse> mouse_;
	bool mouseDone_ = false;
	bool hasWalk_ = false, hasRun_ = false;
	char walkMode_ = 0, runMode_ = 0;
	std::optional<std::pair<float, float>> start_;
	std::optional<float> pitch_, yaw_;
};

} // namespace game

#endif // COMMON_DEBUGAUTOMATION_H_
