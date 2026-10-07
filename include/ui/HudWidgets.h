#if !defined(UI_HUDWIDGETS_H_)
#define UI_HUDWIDGETS_H_

#include "ui/Widget.h"

namespace ui
{

// 単色の矩形(HUDの帯・枠・バーなど)。大きさは setSize で(指定が無ければ親と同じ)。borderを指定すると、その太さの縁を、borderColorで描く
class RectWidget : public Widget
{
public:
	void setColor(float r, float g, float b, float a = 1.0f) { color_ = {r, g, b, a}; }
	void setBorder(float width, float r, float g, float b, float a = 1.0f) { borderWidth_ = width; borderColor_ = {r, g, b, a}; }

protected:
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	Color color_{0.0f, 0.0f, 0.0f, 0.5f};
	float borderWidth_ = 0.0f;
	Color borderColor_;
};

// ミニマップ(カメラの視線の向きが上になるよう、回る)。UiContext::world() のプレイヤー・部屋の境界・点(敵など)を、矩形の中に描く。
// プレイヤーを中心に、矩形の半分が range メートル。プレイヤーの向き(黄)と視野(白い扇)も示す。点は1つ1枚のスプライトなので、数百でも軽い
class MinimapWidget : public Widget
{
public:
	// 矩形の端までの距離(メートル)
	void setRange(float meters) { range_ = meters; }
	void setBackground(float r, float g, float b, float a) { background_ = {r, g, b, a}; }
	void setBorder(float r, float g, float b, float a) { border_ = {r, g, b, a}; }
	void setMarkerSize(float pixels) { markerSize_ = pixels; }

	bool getProperty(const std::string &name, float &value) const override;
	bool setProperty(const std::string &name, float value) override;

protected:
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	float range_ = 8.0f;
	float markerSize_ = 6.0f;
	Color background_{0.0f, 0.0f, 0.0f, 0.45f};
	Color border_{1.0f, 1.0f, 1.0f, 0.8f};
};

} // namespace ui

#endif // UI_HUDWIDGETS_H_
