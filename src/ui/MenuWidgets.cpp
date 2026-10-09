#include "ui/MenuWidgets.h"
#include "geo/Calculator.h"
#include "ui/UiContext.h"
#include "vk/VulkanMath.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ui
{

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float toRadians(float degrees) { return degrees * kPi / 180.0f; }
}

// ---- CarouselWidget ----

int CarouselWidget::selected() const
{
	const int count = static_cast<int>(items_.size());
	if(count == 0){
		return -1;
	}
	const float step = 360.0f / static_cast<float>(count);
	const int index = static_cast<int>(std::lround(rotation_ / step)) % count;
	return index < 0 ? index + count : index;
}

void CarouselWidget::rotate(int delta, float durationSeconds)
{
	if(items_.empty()){
		return;
	}
	const float step = 360.0f / static_cast<float>(items_.size());
	targetRotation_ += static_cast<float>(delta) * step;
	animate("rotation", targetRotation_, durationSeconds, InterpolatorType::Linear); // 連続して回すとき、つなぎ目で速さが変わらないよう等速
}

void CarouselWidget::select(int index, float durationSeconds)
{
	if(items_.empty()){
		return;
	}
	const int count = static_cast<int>(items_.size());
	const float step = 360.0f / static_cast<float>(count);
	// 目標の近くの、同じ項目(360度ずれたもの)のうち、いまの回転にいちばん近いものへ
	const float base = static_cast<float>(((index % count) + count) % count) * step;
	const float turns = std::round((targetRotation_ - base) / 360.0f);
	targetRotation_ = base + turns * 360.0f;
	animate("rotation", targetRotation_, durationSeconds);
}

bool CarouselWidget::getProperty(const std::string &name, float &value) const
{
	if(name == "rotation"){
		value = rotation_;
		return true;
	}
	return Widget::getProperty(name, value);
}

bool CarouselWidget::setProperty(const std::string &name, float value)
{
	if(name == "rotation"){
		rotation_ = value;
		return true;
	}
	return Widget::setProperty(name, value);
}

void CarouselWidget::drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha)
{
	const int count = static_cast<int>(items_.size());
	if(count == 0 || rect.w <= 0.0f || rect.h <= 0.0f){
		return;
	}
	// カメラは原点で-Z向き。円はカメラの前方ringDistanceに置き、X軸まわりに傾ける。
	// 傾けると正面の項目が下がるので、その分(radius*sin(tilt))だけ円を持ち上げて、正面の項目が矩形の中央に残るようにする
	const float tilt = toRadians(tiltDegrees_);
	const float lift = radius_ * std::sin(tilt);
	const auto tiltMatrix = geo::createRotationX<float>(tilt);
	const float delta = 2.0f * kPi / static_cast<float>(count);

	struct Item
	{
		int index;
		float depth;     // カメラ座標系でのz(小さいほど奥)
		float angle;
		float highlight; // 0(通常)〜1(正面で選択中)。回転のアニメーションに合わせて、なめらかに変わる
		float zOffset;
	};
	std::vector<Item> items;
	items.reserve(count);
	for(int i = 0; i < count; ++i){
		// 正面(angle=0)に来た項目が、選択中
		const float angle = delta * static_cast<float>(i) - toRadians(rotation_);
		float wrapped = std::fmod(angle, 2.0f * kPi);
		wrapped = wrapped > kPi ? wrapped - 2.0f * kPi : (wrapped < -kPi ? wrapped + 2.0f * kPi : wrapped);
		const float t = std::clamp(1.0f - std::fabs(wrapped) / delta, 0.0f, 1.0f);
		const float highlight = t * t * (3.0f - 2.0f * t);
		const float zOffset = selectedOffset_ * highlight;
		// 選択時の手前への出っ張り(zOffset)は円の傾きに沿わず、カメラ方向にまっすぐ加える
		const float depth = -ringDistance_ + std::cos(tilt) * radius_ * std::cos(angle) + zOffset;
		items.push_back({i, depth, angle, highlight, zOffset});
	}
	// 奥から手前の順に描く(アルファブレンドのため)
	std::sort(items.begin(), items.end(), [](const Item &a, const Item &b){ return a.depth < b.depth; });

	// 矩形に収める射影: 矩形の縦横比で透視投影して、クリップ空間で矩形の位置・大きさへ移す(画面の座標は、y下向き)
	const auto proj = vk_::createPerspective(toRadians(viewAngle_), rect.w, rect.h, 1.0f, 200.0f);
	const float screenW = ctx.screenWidth(), screenH = ctx.screenHeight();
	const auto viewport = geo::createTranslation<float>(geo::Vector3f((rect.x + rect.w * 0.5f) / screenW * 2.0f - 1.0f, (rect.y + rect.h * 0.5f) / screenH * 2.0f - 1.0f, 0.0f))
		* geo::createScale<float>(geo::Vector3f(rect.w / screenW, rect.h / screenH, 1.0f));
	const auto viewProj = viewport * proj;

	for(const auto &item : items){
		// 円の座標系での位置(円は水平面上: y=0)
		const geo::Vector3f pos(radius_ * std::sin(item.angle), 0.0f, radius_ * std::cos(item.angle));
		const auto ring = geo::createTranslation<float>(geo::Vector3f(0.0f, lift, -ringDistance_ + item.zOffset)) * tiltMatrix;
		// billboard: 円の傾き(X軸まわり)だけを打ち消して正面を向ける。Y軸まわりは回さない。そうでなければ円の外側へ向ける
		const auto facing = billboard_ ? geo::createRotationX<float>(-tilt) : geo::createRotationY<float>(item.angle);
		const auto model = ring * geo::createTranslation<float>(pos) * facing * geo::createScale<float>(geo::Vector3f(itemWidth_, itemHeight_, 1.0f));
		const float itemAlpha = alphaNormal_ + (alphaSelected_ - alphaNormal_) * item.highlight;
		ctx.drawTexture3D(ctx.image(items_[static_cast<size_t>(item.index)]), viewProj * model, Color{1.0f, 1.0f, 1.0f, itemAlpha}, alpha);
	}
}

// ---- StripWidget ----

void StripWidget::select(int index, float durationSeconds)
{
	if(items_.empty()){
		return;
	}
	selected_ = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
	animate("scroll", -(itemWidth_ + gap_) * static_cast<float>(selected_), durationSeconds);
}

bool StripWidget::getProperty(const std::string &name, float &value) const
{
	if(name == "scroll"){
		value = scroll_;
		return true;
	}
	return Widget::getProperty(name, value);
}

bool StripWidget::setProperty(const std::string &name, float value)
{
	if(name == "scroll"){
		scroll_ = value;
		return true;
	}
	return Widget::setProperty(name, value);
}

void StripWidget::drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha)
{
	if(background_.a > 0.0f){
		ctx.drawTexture(ctx.white(), rect, Color{background_.r, background_.g, background_.b, background_.a}, alpha);
	}
	// アイコンは帯の縦中央。選択中のアイコンが、矩形の横の中央に来る
	const float y = rect.y + (rect.h - itemHeight_) * 0.5f;
	for(size_t i = 0; i < items_.size(); ++i){
		const float x = rect.x + rect.w * 0.5f + scroll_ + (itemWidth_ + gap_) * static_cast<float>(i) - itemWidth_ * 0.5f;
		if(x + itemWidth_ < 0.0f || x > ctx.screenWidth()){
			continue; // 画面外
		}
		ctx.drawTexture(ctx.image(items_[i]), LayoutRect{x, y, itemWidth_, itemHeight_}, Color{}, alpha);
	}
}

} // namespace ui
