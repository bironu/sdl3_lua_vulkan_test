#ifndef SDLBITMAPFONT_H_
#define SDLBITMAPFONT_H_

#include "misc/Uncopyable.h"
#include "sdl/SDLImage.h"
#include <array>
#include <memory>

namespace SDL_
{
class Color;

// TtfFontで ASCII 0x20-0x7E を白で1枚のImageに描画しておき、文字ごとに切り出してblitするフォント。
// 数字など頻繁に書き換わる文字列でも、都度TtfFontでImageを生成しなくて済む。
// 等幅フォントなら全文字を一発描画、等幅でなければ1文字ずつ幅を測って並べる。
// 範囲外の文字(改行等)は空白として扱う
class BitmapFont
{
public:
	UNCOPYABLE(BitmapFont);
	static constexpr int FirstChar = 0x20;
	static constexpr int GlyphCount = 0x7F - FirstChar;

	BitmapFont(const char *fontFile, float ptsize);
	~BitmapFont();

	bool isValid() const { return atlas_ != nullptr; }
	int getLineHeight() const { return lineHeight_; }
	// 全文字を並べた1枚の画像(白・アルファつき)と、その中での文字の矩形(範囲外の文字は空白)。GPUのテクスチャにして、1文字ずつ描くのに使う
	const std::shared_ptr<Image> &atlas() const { return atlas_; }
	Rect glyphRect(unsigned char ch) const { return glyphOf(ch).rect; }
	int getTextWidth(const char *text) const;
	// 色は白で生成済み。fgはsetColorModで乗算される
	void drawText(std::shared_ptr<Image> dst, int x, int y, const char *text, const Color &fg) const;

private:
	const SubImage &glyphOf(unsigned char ch) const;

	std::shared_ptr<Image> atlas_;
	std::array<SubImage, GlyphCount> glyphs_;
	int lineHeight_ = 0;
};

} // namespace SDL_

#endif // SDLBITMAPFONT_H_
