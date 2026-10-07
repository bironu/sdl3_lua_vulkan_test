#include "sdl/SDLBitmapFont.h"
#include "sdl/SDLTtfFont.h"
#include "sdl/SDLColor.h"
#include "geo/Rect.h"
#include <string>

namespace SDL_
{

BitmapFont::BitmapFont(const char *fontFile, float ptsize)
{
	TtfFont font(fontFile, ptsize);
	if (!font.is_font()) {
		return;
	}

	std::array<int, GlyphCount> widths{};
	bool monospace = true;
	std::string all;
	for (int i = 0; i < GlyphCount; ++i) {
		const char s[2] = {static_cast<char>(FirstChar + i), '\0'};
		geo::Sizei size;
		if (!font.getTextSize(s, &size)) {
			return;
		}
		widths[i] = size.getWidth();
		lineHeight_ = size.getHeight();
		monospace = monospace && widths[i] == widths[0];
		all += s;
	}

	const Color white(255, 255, 255, 255);
	if (monospace) {
		// 等幅: 全文字を一発描画。セル幅は固定
		atlas_ = font.renderBlendedText(all.c_str(), white);
		if (!atlas_) {
			return;
		}
		for (int i = 0; i < GlyphCount; ++i) {
			glyphs_[i] = {atlas_, Rect(i * widths[0], 0, widths[0], lineHeight_)};
		}
	}
	else {
		// 非等幅: 1文字ずつ描画して、測定幅で横に並べる
		int total = 0;
		for (int w : widths) {
			total += w;
		}
		atlas_ = std::make_shared<Image>(total, lineHeight_);
		int x = 0;
		for (int i = 0; i < GlyphCount; ++i) {
			const char s[2] = {static_cast<char>(FirstChar + i), '\0'};
			if (auto glyph = font.renderBlendedText(s, white)) {
				glyph->setBlendMode(SDL_BLENDMODE_NONE); // アルファ込みでそのままコピー
				atlas_->blit(glyph, x, 0);
			}
			glyphs_[i] = {atlas_, Rect(x, 0, widths[i], lineHeight_)};
			x += widths[i];
		}
	}
	atlas_->setBlendMode(SDL_BLENDMODE_BLEND);
}

BitmapFont::~BitmapFont() = default;

const SubImage &BitmapFont::glyphOf(unsigned char ch) const
{
	const int idx = (ch >= FirstChar && ch < FirstChar + GlyphCount) ? ch - FirstChar : 0;
	return glyphs_[idx];
}

int BitmapFont::getTextWidth(const char *text) const
{
	if (!atlas_) {
		return 0;
	}
	int width = 0;
	for (const char *p = text; *p; ++p) {
		width += glyphOf(static_cast<unsigned char>(*p)).rect.w;
	}
	return width;
}

void BitmapFont::drawText(std::shared_ptr<Image> dst, int x, int y, const char *text, const Color &fg) const
{
	if (!dst || !atlas_) {
		return;
	}

	atlas_->setColorMod(fg.getRed(), fg.getGreen(), fg.getBlue());
	for (const char *p = text; *p; ++p) {
		const SubImage &g = glyphOf(static_cast<unsigned char>(*p));
		dst->blit(g.sheet, g.rect, static_cast<Sint16>(x), static_cast<Sint16>(y));
		x += g.rect.w;
	}
}

} // namespace SDL_
