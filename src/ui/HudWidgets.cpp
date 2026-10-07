#include "ui/HudWidgets.h"
#include "ui/UiContext.h"
#include <algorithm>
#include <cmath>

namespace ui
{

namespace
{
// 矩形(論理画面の座標)をはみ出さない部分だけを、単色で描く
void fillRect(UiContext &ctx, const LayoutRect &clip, float x, float y, float w, float h, const Color &color, float alpha)
{
	const float x0 = std::max(x, clip.x), y0 = std::max(y, clip.y);
	const float x1 = std::min(x + w, clip.x + clip.w), y1 = std::min(y + h, clip.y + clip.h);
	if(x1 > x0 && y1 > y0){
		ctx.drawTexture(ctx.white(), LayoutRect{x0, y0, x1 - x0, y1 - y0}, color, alpha);
	}
}

void strokeRect(UiContext &ctx, const LayoutRect &clip, const LayoutRect &r, float width, const Color &color, float alpha)
{
	fillRect(ctx, clip, r.x, r.y, r.w, width, color, alpha);
	fillRect(ctx, clip, r.x, r.y + r.h - width, r.w, width, color, alpha);
	fillRect(ctx, clip, r.x, r.y + width, width, r.h - width * 2.0f, color, alpha);
	fillRect(ctx, clip, r.x + r.w - width, r.y + width, width, r.h - width * 2.0f, color, alpha);
}
}

void RectWidget::drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha)
{
	if(color_.a > 0.0f){
		ctx.drawTexture(ctx.white(), rect, color_, alpha);
	}
	if(borderWidth_ > 0.0f){
		strokeRect(ctx, rect, rect, borderWidth_, borderColor_, alpha);
	}
}

bool MinimapWidget::getProperty(const std::string &name, float &value) const
{
	if(name == "range"){
		value = range_;
		return true;
	}
	return Widget::getProperty(name, value);
}

bool MinimapWidget::setProperty(const std::string &name, float value)
{
	if(name == "range"){
		range_ = value;
		return true;
	}
	return Widget::setProperty(name, value);
}

void MinimapWidget::drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha)
{
	const WorldView &world = ctx.world();
	fillRect(ctx, rect, rect.x, rect.y, rect.w, rect.h, background_, alpha);

	// 視線の向きが上になるように、ワールドの点を、プレイヤー中心の矩形の座標(ピクセル。右が+x、下が+y)へ回す。
	// カメラの視線は(-sin yaw, -cos yaw)、その右は(cos yaw, -sin yaw)
	const float scale = std::min(rect.w, rect.h) * 0.5f / std::max(range_, 0.01f); // ピクセル/メートル
	const float sinY = std::sin(world.cameraYaw), cosY = std::cos(world.cameraYaw);
	const float cx = rect.x + rect.w * 0.5f, cy = rect.y + rect.h * 0.5f;
	const auto toLocal = [&](float dx, float dz, float &lx, float &ly){
		lx = (dx * cosY - dz * sinY) * scale;
		ly = (dx * sinY + dz * cosY) * scale; // 前(上)がマイナス
	};
	const float halfW = rect.w * 0.5f, halfH = rect.h * 0.5f;

	// 矩形の中に収まるように切って、線を描く(矩形の中心を原点とした座標)
	const auto line = [&](float x0, float y0, float x1, float y1, float thickness, const Color &color){
		float t0 = 0.0f, t1 = 1.0f;
		const float dx = x1 - x0, dy = y1 - y0;
		const float p[4] = {-dx, dx, -dy, dy};
		const float q[4] = {x0 + halfW, halfW - x0, y0 + halfH, halfH - y0};
		for(int i = 0; i < 4; ++i){
			if(p[i] == 0.0f){
				if(q[i] < 0.0f){
					return;
				}
			}
			else{
				const float t = q[i] / p[i];
				if(p[i] < 0.0f){
					t0 = std::max(t0, t);
				}
				else{
					t1 = std::min(t1, t);
				}
			}
		}
		if(t0 >= t1){
			return;
		}
		const float ax = x0 + dx * t0, ay = y0 + dy * t0, bx = x0 + dx * t1, by = y0 + dy * t1;
		const float length = std::hypot(bx - ax, by - ay);
		if(length > 0.01f){
			ctx.drawTextureRotated(ctx.white(), LayoutRect{cx + (ax + bx) * 0.5f - length * 0.5f, cy + (ay + by) * 0.5f - thickness * 0.5f, length, thickness},
				std::atan2(by - ay, bx - ax), color, alpha);
		}
	};

	// 動ける範囲の境界(長方形の四辺)
	if(world.boundsMaxX > world.boundsMinX && world.boundsMaxZ > world.boundsMinZ){
		float corner[4][2];
		const float xs[4] = {world.boundsMinX, world.boundsMaxX, world.boundsMaxX, world.boundsMinX};
		const float zs[4] = {world.boundsMinZ, world.boundsMinZ, world.boundsMaxZ, world.boundsMaxZ};
		for(int i = 0; i < 4; ++i){
			toLocal(xs[i] - world.playerX, zs[i] - world.playerZ, corner[i][0], corner[i][1]);
		}
		for(int i = 0; i < 4; ++i){
			const int next = (i + 1) % 4;
			line(corner[i][0], corner[i][1], corner[next][0], corner[next][1], 3.0f, Color{0.8f, 0.9f, 1.0f, 0.9f});
		}
	}
	// 点(敵など)
	const float half = markerSize_ * 0.5f;
	for(const auto &marker : world.markers){
		float lx, ly;
		toLocal(marker.x - world.playerX, marker.z - world.playerZ, lx, ly);
		fillRect(ctx, rect, cx + lx - half, cy + ly - half, markerSize_, markerSize_, marker.color, alpha);
	}
	// 視野(上向きの扇の両端)
	const float reach = std::min(halfW, halfH);
	for(int side = -1; side <= 1; side += 2){
		const float angle = side * 0.6f;
		line(0.0f, 0.0f, std::sin(angle) * reach, -std::cos(angle) * reach, 2.0f, Color{1.0f, 1.0f, 1.0f, 0.5f});
	}
	// プレイヤー(緑の点)と、向き(黄色の線)
	float hx, hy;
	toLocal(std::sin(world.heading), std::cos(world.heading), hx, hy);
	const float hl = std::hypot(hx, hy);
	if(hl > 0.0f){
		line(0.0f, 0.0f, hx / hl * 26.0f, hy / hl * 26.0f, 4.0f, Color{1.0f, 0.9f, 0.2f, 1.0f});
	}
	fillRect(ctx, rect, cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, Color{0.2f, 1.0f, 0.4f, 1.0f}, alpha);
	strokeRect(ctx, rect, rect, 2.0f, border_, alpha);
}

} // namespace ui
