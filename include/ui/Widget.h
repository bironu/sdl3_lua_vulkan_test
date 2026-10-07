#if !defined(UI_WIDGET_H_)
#define UI_WIDGET_H_

#include "task/Interpolator.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui
{

class UiContext;

// アニメーションの繰り返し方
enum class Loop
{
	Once,     // 1回だけ(最後で止まる)
	Repeat,   // 始めの値へ戻って、繰り返す
	PingPong, // 目標まで行ったら、始めの値まで戻る、を繰り返す(ふわふわ)
};

// ウィジェットの配置結果(論理画面の座標。左上原点・y下向き)
struct LayoutRect
{
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
};

// 色(各成分0〜1)
struct Color
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

// 2Dの描画物の基底クラス。Compositeパターン: 子を持てる(GroupWidgetは子を取りまとめるだけ)。
// Luaスクリプトから生成・操作される(ui/UiScript)が、描画はC++側が行う(drawTree)。
//
// 配置: 親の矩形の中の「アンカー」(親の矩形の何割の位置か。0〜1)に、自分の「ピボット」(自分の矩形の何割の位置か)を合わせ、
// そこからの位置(x, y)だけずらす。例: 画面の真ん中に文字を置く = アンカー(0.5, 0.5)、ピボット(0.5, 0.5)、位置(0, 0)。
// 大きさは、指定(setSize)があればそれ、無ければ固有の大きさ(画像・文字の大きさ)、それも無ければ親と同じ大きさ。
// 所有: 木(親)がshared_ptrで子を持つ。Luaのハンドルもshared_ptrなので、Luaが保持していても、木から外されても、
// 安全に使え(ダングリングなし)、最後の参照が無くなると自動で解放される
class Widget : public std::enable_shared_from_this<Widget>
{
public:
	Widget();
	virtual ~Widget() = default;
	Widget(const Widget &) = delete;
	Widget &operator=(const Widget &) = delete;

	uint32_t id() const { return id_; }
	const std::string &name() const { return name_; }
	void setName(const std::string &name) { name_ = name; }

	void setPos(float x, float y) { x_ = x; y_ = y; }
	float x() const { return x_; }
	float y() const { return y_; }
	// 大きさの指定(0以下なら、固有の大きさ→親と同じ、の順に決める)
	void setSize(float w, float h) { width_ = w; height_ = h; }
	void setAnchor(float ax, float ay) { anchorX_ = ax; anchorY_ = ay; }
	void setPivot(float px, float py) { pivotX_ = px; pivotY_ = py; }
	void setVisible(bool visible) { visible_ = visible; }
	bool visible() const { return visible_; }
	// 不透明度(0〜1。子にも掛かる)
	void setAlpha(float alpha) { alpha_ = alpha; }
	float alpha() const { return alpha_; }
	// 拡大率(1が等倍)。ピボットを動かさずに、大きさと位置のずらし(x, y)が掛かる。子孫にも掛かる(子の大きさ・位置も同じ率で)
	void setScale(float scale) { scale_ = scale; }
	float scale() const { return scale_; }
	// 回転(度。時計回りが正)。自分の描画だけが、矩形の中心のまわりに回る(子は回らない)
	void setAngle(float degrees) { angleDegrees_ = degrees; }
	float angle() const { return angleDegrees_; }

	// 数値のプロパティを、時間をかけて目標値まで変える(Luaの widget:animate("x", 100, 0.2, "accelerateDecelerate"))。
	// プロパティ名: x, y, alpha, width, height, scale, angle(派生クラスは、さらに増やす。Carouselのrotation、Stripのscrollなど)。
	// 動きの補間は、task/Interpolator.hの種類すべて。loopで繰り返し(Repeat/PingPong。止めるのはstopAnimation)、delayで開始を遅らせる。
	// 同じプロパティのアニメーションが進行中なら、置き換える。durationが0以下なら、すぐに設定する(loopは無視)。名前が違えばfalse
	bool animate(const std::string &property, float target, float durationSeconds, InterpolatorType interpolator = InterpolatorType::AccelerateDecelerate,
		Loop loop = Loop::Once, float delaySeconds = 0.0f);
	// プロパティのアニメーションを止める(値はそのまま)。propertyが空なら、このウィジェットの全て
	void stopAnimation(const std::string &property = std::string());
	bool isAnimating() const { return !tweens_.empty(); }
	// 自分と子孫のアニメーションを進める(UiScriptが、毎フレームupdateの前に呼ぶ)
	void updateAnimations(float dt);
	// プロパティの現在値/設定。名前が違えばfalse
	virtual bool getProperty(const std::string &name, float &value) const;
	virtual bool setProperty(const std::string &name, float value);

	// マウスの判定(hover/click)の対象にするか。UiScriptのonEnter/onLeave/onClickを設定すると、自動でtrueになる。見えている(visible)ものだけが対象
	void setInteractive(bool interactive) { interactive_ = interactive; }
	bool interactive() const { return interactive_; }
	// マウスカーソルが乗っているか(UiScriptが、マウス移動・毎フレームの更新で決める。重なっているときは、最前面の1つだけ)
	bool hovered() const { return hovered_; }
	void setHovered(bool hovered) { hovered_ = hovered; }
	// 論理画面の点(x, y)が、このウィジェットの矩形(拡大率込み。回転は無視)の中か
	bool contains(UiContext &ctx, float x, float y);
	// 自分と子孫のうち、点(x, y)にある、最前面のinteractiveなウィジェット(無ければnullptr)。parentRect: 親の矩形
	std::shared_ptr<Widget> pick(UiContext &ctx, const LayoutRect &parentRect, float x, float y);

	// 子を末尾(最前面)に足す。すでに別の親の子なら、そこから外す。自分自身・祖先は足せない(falseを返す)
	bool add(const std::shared_ptr<Widget> &child);
	bool remove(const std::shared_ptr<Widget> &child);
	void clearChildren();
	// 名前で子孫を探す(自分を含む。最初に見つかったもの)
	std::shared_ptr<Widget> find(const std::string &name);
	Widget *parent() const { return parent_; }
	const std::vector<std::shared_ptr<Widget>> &children() const { return children_; }

	// 固有の大きさ(画像・文字の、大きさの指定に依らない大きさ)。無ければfalse(Luaの getNaturalWidth/Height)
	bool naturalSize(UiContext &ctx, float &w, float &h) { return intrinsicSize(ctx, w, h); }
	// 配置を解決した結果(parentRect: 親の矩形)。固有の大きさが要るウィジェットは、ctxでテクスチャを作る
	LayoutRect resolve(UiContext &ctx, const LayoutRect &parentRect);
	// 親をたどって、このウィジェットの現在の矩形を求める(Luaから大きさ・位置を知るため)
	LayoutRect resolvedRect(UiContext &ctx);
	// 自分と子孫を描く(木の順に、後ろほど手前)
	void draw(UiContext &ctx, const LayoutRect &parentRect, float parentAlpha);

protected:
	// 固有の大きさ(画像・文字の大きさ)。無ければfalse
	virtual bool intrinsicSize(UiContext &, float &, float &) { return false; }
	// 自分自身の描画(子は含まない)
	virtual void drawSelf(UiContext &, const LayoutRect &, float) {}

private:
	float effectiveScale() const; // 祖先の拡大率との積
	bool isAncestorOrSelf(const Widget *other) const;

	struct Tween
	{
		std::string property;
		float from = 0.0f;
		float to = 0.0f;
		float elapsed = 0.0f;
		float duration = 0.0f;
		float delay = 0.0f;
		Loop loop = Loop::Once;
		std::function<float(float)> interpolate;
	};
	std::vector<Tween> tweens_;
	uint32_t id_;
	std::string name_;
	float x_ = 0.0f, y_ = 0.0f;
	float width_ = 0.0f, height_ = 0.0f;
	float anchorX_ = 0.0f, anchorY_ = 0.0f;
	float pivotX_ = 0.0f, pivotY_ = 0.0f;
	bool visible_ = true;
	bool interactive_ = false;
	bool hovered_ = false;
	float alpha_ = 1.0f;
	float scale_ = 1.0f;
	float angleDegrees_ = 0.0f;
	Widget *parent_ = nullptr;
	std::vector<std::shared_ptr<Widget>> children_;
};

} // namespace ui

#endif // UI_WIDGET_H_
