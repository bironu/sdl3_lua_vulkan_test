#if !defined(UI_UISCRIPT_H_)
#define UI_UISCRIPT_H_

#include "ui/UiContext.h"
#include "ui/Widgets.h"
#include <functional>
#include <memory>
#include <string>

namespace ui
{

// Luaスクリプトでウィジェットの木を作って操作し、描画はC++が行うための仕組み。
//
// スクリプト(res/lua/ui/*.lua)が定義する関数(どれも無くてよい):
//   init()                       読み込み直後に1回。ウィジェットを作って root に足す
//   update(dt, time)             毎フレーム。位置・可視・色などを変える(dt: 前フレームからの秒、time: シーン開始からの秒)
//   onKey(key, down)             キー/ゲームパッドのボタンの押下/離上(keyはSDL_GetKeyNameの名前: "Return", "Up", "Space", "A" など、または"PadA"など)
//   onMouseMove(x, y)            マウス移動(論理画面の座標)
//   onMouseButton(button, down, x, y)
// スクリプトから使えるもの:
//   root                         画面サイズのGroupWidget(木の根)
//   ui.group() / ui.image(path) / ui.text(str, size) / ui.background()   ウィジェットを作る(戻り値がハンドル。w:setPos(x, y) のように操作)
//   scene.change(name) / scene.quit()   シーン遷移・終了
//   screen.width / screen.height        論理画面の大きさ
//   input.isDown(name)                  キー/ゲームパッドのボタンが押されているか(名前: "W", "Return" / "PadA", "PadLeft" など。ui/PadNames.h)
//   input.axis(name)                    ゲームパッドの軸(-1〜1): "LeftX", "LeftY", "RightX", "RightY"(下が+)、"LeftTrigger", "RightTrigger"
//   audio.play(path)                    効果音を鳴らす
//   loadScript(path)                    別のLuaファイルを、同じ状態で実行する(データの定義を読むのに使う)
//   game.command(name, value)           シーンへ命令を送る(シーンごとに決まる名前。例: GameSceneの "resume" / "sensitivity")
//   w:animate(property, target, seconds, interpolator, {loop=, delay=})  プロパティを時間をかけて変える。
//        property: x y alpha width height scale angle(度)、Carouselのrotation、Stripのscroll、Minimapのrange
//        interpolator(task/Interpolator.hの全種類): "accelerateDecelerate"(省略時) "accelerate" "decelerate" "anticipate" "anticipateOvershoot" "bounce" "cycle" "linear" "overshoot"
//        loop: "once"(省略時) "repeat" "pingpong"(ふわふわ。w:stopAnimation(property)で止める)、delay: 開始までの秒
//        例: icon:animate("scale", 1.15, 0.6, "accelerateDecelerate", {loop = "pingpong"})
//   w:onEnter(fn) / w:onLeave(fn) / w:onClick(fn)   マウスカーソルが乗った/出た/クリックされた(fn(widget)、onClickはfn(widget, ボタン番号))。nilで解除。設定すると、そのウィジェットがマウス判定の対象になる
//        重なっているときは、最前面の1つだけ。見えていない(setVisible(false))ものは対象外。拡大率は反映、回転は無視。w:isHovered() / w:contains(x, y) / w:setInteractive(bool)
//   w:setScale(s) / w:setAngle(度)       拡大率(ピボット基準。子孫にも掛かる)・回転(自分の描画だけ)
//   text:setOutline(太さ, R, G, B)       文字の縁取り(太さ0で無し)
//   text:setBitmap(true) / ui.text(s, {bitmap = true})   ビットマップフォント(ASCIIだけ)で描く。毎フレーム変わる数字(FPS・座標)は、これにすると、文字を作り直さず軽い
//   ui.rect() / ui.minimap()            単色の矩形(setColor, setBorder)・ミニマップ(setRange(メートル)。ui/HudWidgets.h)
//   world.x / z / heading / cameraYaw / fps / sensitivity(と、シーンが足した数値)   ゲームの状態(読み取り専用。GameSceneなどが毎フレーム、updateの前に更新する)
//   ui.carousel() / ui.strip()          回転メニュー・横スクロールの帯(ui/MenuWidgets.h)
// onKeyには、ゲームパッドの十字キー・左スティック(上下左右を押している間は、キーリピートのように繰り返す)・ボタンも、"PadLeft"などの名前で届く
//   log(...) / print(...)               ログに出す
// ウィジェットのハンドルはshared_ptrで、Luaが持っていても安全(解放漏れ・ダングリングなし)。
// load()をもう一度呼ぶと、Luaの状態も木も作り直す(スクリプトのリロード。エラーがあればログに出して、空の木のまま続ける)
class UiScript
{
public:
	struct Callbacks
	{
		std::function<void(const std::string &)> changeScene;
		std::function<void()> quit;
		std::function<void(const std::string &)> playSound; // 効果音(リポジトリ直下からのパス)を鳴らす
		std::function<void(const std::string &, double)> command; // スクリプトの game.command(名前, 値) を、シーンへ伝える(ポーズの再開・オプションの変更など)
	};

	UiScript(UiContext &ctx, Callbacks callbacks);
	~UiScript();
	UiScript(const UiScript &) = delete;
	UiScript &operator=(const UiScript &) = delete;

	// リポジトリ直下からの相対パス(例: "res/lua/ui/opening.lua")のスクリプトを読んで、init()を呼ぶ。成功したらtrue
	bool load(const std::string &path);
	void update(float dt, float time);
	void onKey(const std::string &keyName, bool down);
	void onMouseMove(float x, float y);
	void onMouseButton(int button, bool down, float x, float y);
	// マウスカーソルが、interactiveなウィジェット(onEnter/onLeave/onClickを持つ、またはsetInteractive(true))の上にあるか(画面の部品の上かどうかで、3D側の操作と分ける)
	bool hasHover() const;
	// 木を描く(描画の予約)
	void draw();

private:
	void updateHover();
	void pushWorld();
	struct Impl;
	std::unique_ptr<Impl> impl_;
	UiContext &ctx_;
	Callbacks callbacks_;
};

} // namespace ui

#endif // UI_UISCRIPT_H_
