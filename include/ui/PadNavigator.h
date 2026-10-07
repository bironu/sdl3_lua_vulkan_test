#if !defined(UI_PADNAVIGATOR_H_)
#define UI_PADNAVIGATOR_H_

#include <cstdint>

class Resources;

namespace ui
{

class UiScript;

// ゲームパッドの十字キー・左スティックを、上下左右のキー("PadUp"/"PadDown"/"PadLeft"/"PadRight")として、
// 押している間は繰り返して(キーリピートのように)UiScriptのonKeyへ渡す(メニュー操作用)。毎フレーム poll() を呼ぶ
class PadNavigator
{
public:
	// 渡したあとにシーンが終わった(スクリプトからscene.changeなどが呼ばれた)かどうかは、呼び出し側が見る
	void poll(Resources &res, UiScript &script, uint32_t tick);
	// 押している状態を忘れる(画面を切り替えたとき。次のpollで、押したままでも新しい押下として扱う)
	void reset();

private:
	struct Direction
	{
		bool pressed = false;
		uint32_t nextRepeat = 0;
	};
	Direction directions_[4]; // 上・下・左・右
};

} // namespace ui

#endif // UI_PADNAVIGATOR_H_
