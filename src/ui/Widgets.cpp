#include "ui/Widgets.h"
#include "ui/UiContext.h"

namespace ui
{

void ImageWidget::setImage(const std::string &path)
{
	path_ = path;
}

bool ImageWidget::intrinsicSize(UiContext &ctx, float &w, float &h)
{
	const auto texture = ctx.image(path_);
	if(!texture){
		return false;
	}
	w = static_cast<float>(texture->width());
	h = static_cast<float>(texture->height());
	return true;
}

void ImageWidget::drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha)
{
	if(const auto texture = ctx.image(path_)){
		ctx.drawTexture(texture, rect, tint_, alpha);
	}
}

void TextWidget::setText(const std::string &text)
{
	if(!key_.empty()){
		key_.clear();
		dirty_ = true;
	}
	if(text != text_){
		text_ = text;
		dirty_ = true;
	}
}

void TextWidget::setTextKey(const std::string &key)
{
	if(key != key_){
		key_ = key;
		resolvedVersion_ = 0; // 次の描画で解決し直す
		dirty_ = true;
	}
}

void TextWidget::setTextVars(const std::map<std::string, std::string> &vars)
{
	vars_ = vars;
	resolvedVersion_ = 0;
	dirty_ = true;
}

void TextWidget::setFont(const std::string &name)
{
	if(name != fontName_){
		fontName_ = name;
		dirty_ = true;
	}
}

void TextWidget::setFontSize(float size)
{
	if(size != fontSize_){
		fontSize_ = size;
		dirty_ = true;
	}
}

void TextWidget::resolveText(UiContext &ctx)
{
	// キー指定なら、言語(定義)が変わったときに、文字列を解決し直す
	if(!key_.empty() && resolvedVersion_ != ctx.languageVersion() + 1){
		resolvedVersion_ = ctx.languageVersion() + 1;
		text_ = ctx.translate(key_, vars_);
		dirty_ = true;
	}
	// フォント定義(言語ごとに違うことがある)が変わった可能性も、languageVersionで検出する
	if(fontVersion_ != ctx.languageVersion() + 1){
		fontVersion_ = ctx.languageVersion() + 1;
		dirty_ = true;
	}
}

void TextWidget::ensureTexture(UiContext &ctx)
{
	resolveText(ctx);
	if(dirty_){
		texture_ = text_.empty() ? nullptr : ctx.renderText(text_, fontName_, fontSize_);
		dirty_ = false;
	}
}

bool TextWidget::intrinsicSize(UiContext &ctx, float &w, float &h)
{
	if(bitmap_){
		resolveText(ctx);
		const auto *glyphs = ctx.bitmapFont(fontName_, fontSize_);
		if(!glyphs || text_.empty()){
			return false;
		}
		w = glyphs->textWidth(text_);
		h = glyphs->lineHeight();
		return true;
	}
	ensureTexture(ctx);
	if(!texture_){
		return false;
	}
	w = static_cast<float>(texture_->width());
	h = static_cast<float>(texture_->height());
	return true;
}

void TextWidget::drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha)
{
	if(bitmap_){
		resolveText(ctx);
		const auto *glyphs = ctx.bitmapFont(fontName_, fontSize_);
		if(!glyphs || text_.empty()){
			return;
		}
		const float natural = glyphs->textWidth(text_);
		const float scale = natural > 0.0f ? rect.w / natural : 1.0f; // 拡大(scale)・大きさの指定に合わせる
		if(outlineWidth_ > 0.0f){
			static const float kDirections[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
			for(const auto &direction : kDirections){
				ctx.drawBitmapText(*glyphs, text_, rect.x + direction[0] * outlineWidth_ * scale, rect.y + direction[1] * outlineWidth_ * scale, scale, outlineColor_, alpha);
			}
		}
		ctx.drawBitmapText(*glyphs, text_, rect.x, rect.y, scale, color_, alpha);
		return;
	}
	ensureTexture(ctx);
	if(texture_){
		if(outlineWidth_ > 0.0f){
			// 縁取り(先に、8方向へずらして描く)。拡大(scale)しているときは、縁の太さも同じ率で
			const float ratio = texture_->width() > 0 ? rect.w / static_cast<float>(texture_->width()) : 1.0f;
			const float d = outlineWidth_ * ratio;
			static const float kDirections[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
			for(const auto &direction : kDirections){
				ctx.drawTexture(texture_, LayoutRect{rect.x + direction[0] * d, rect.y + direction[1] * d, rect.w, rect.h}, outlineColor_, alpha);
			}
		}
		ctx.drawTexture(texture_, rect, color_, alpha);
	}
}

bool BackgroundWidget::intrinsicSize(UiContext &ctx, float &w, float &h)
{
	w = ctx.screenWidth();
	h = ctx.screenHeight();
	return true;
}

void BackgroundWidget::drawSelf(UiContext &ctx, const LayoutRect &, float alpha)
{
	// 常に画面全体(位置・親に依らない)
	const LayoutRect screen{0.0f, 0.0f, ctx.screenWidth(), ctx.screenHeight()};
	if(!path_.empty()){
		if(const auto texture = ctx.image(path_)){
			ctx.drawTexture(texture, screen, Color{1.0f, 1.0f, 1.0f, color_.a}, alpha);
			return;
		}
	}
	ctx.drawTexture(ctx.white(), screen, color_, alpha);
}

} // namespace ui
