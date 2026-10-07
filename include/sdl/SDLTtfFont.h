#ifndef SDLTTFFONT_H_
#define SDLTTFFONT_H_

#include "misc/Uncopyable.h"
#include "geo/Vector2.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SDL_
{
class Color;
class Image;
class TtfFont
{
public:
	UNCOPYABLE(TtfFont);
	TtfFont(const char *file, float ptsize)
		: font_(::TTF_OpenFont(file, ptsize))
		, fontSize_(ptsize)
	{
	}
	~TtfFont();

	float getFontSize() const { return fontSize_; }
	TTF_Font *get() const { return font_; }
	bool is_font() const { return font_; }

	bool getTextSize(const char *text, geo::Sizei *size);
	std::shared_ptr<SDL_::Image> renderSolidText(const char *text, const SDL_::Color &fg);
	std::shared_ptr<SDL_::Image> renderShadedText(const char *text, const SDL_::Color &fg, const SDL_::Color &bg);
	std::shared_ptr<SDL_::Image> renderBlendedText(const char *text, const SDL_::Color &fg);

	// ---- 既存のImageへ、直接描く(TTF_DrawSurfaceText) ----
	// 描画のたびにSurfaceを作って捨てない(メモリの確保・解放を繰り返さない)。大量の文字列・文字送りの本文向き。
	// 内部に、文字列の入れ物(TTF_Text)を1つ持って使い回す。同じTtfFontを複数スレッドから同時に使わないこと。
	// dstは、文字を載せる画像(アルファつきのフォーマット推奨。背景は呼び出し側が塗っておく)。(x, y)は文字列の左上。成功したらtrue
	bool drawText(Image &dst, const char *text, int x, int y, const Color &fg);

	// 行分けの結果の1行。firstChar/charCountは、全体の何文字目から何文字か(改行コードは数えない。UTF-8のコードポイント単位)。
	// 文字単位の情報があるので、文字送り(先頭からN文字だけ描く)や、後からルビ(文字の範囲への注釈)を足すのに使える
	struct TextLine
	{
		std::string text; // この行の文字列(改行コードは含まない)
		int width = 0;    // 行の幅(ピクセル。ぶら下げの句読点を含む)
		int firstChar = 0;
		int charCount = 0;
	};
	// 本文を、maxWidth(ピクセル)に収まるよう、禁則処理つきで行に分ける。"\n"は強制改行。
	//   行頭禁止: 、。，．・：；？！ー)」』】…などと小書きのかな。行末禁止: (「『【など。分離禁止: ……・――。英数字の単語は途中で切らない。
	//   追い出し: 行頭に来てはいけない文字が溢れるときは、直前の文字ごと次の行へ。hangPunctuationなら、句読点(、。，．)だけは、行末にぶら下げて、はみ出してよい。
	// 幅は文字ごとの送り幅の合計(カーニングは見ない。日本語向き)
	std::vector<TextLine> layoutLines(const char *text, int maxWidth, bool hangPunctuation = true) const;
	// layoutLinesの結果を、(x, y)から描く。行の間隔は、フォントの行の高さ+lineSpacing。visibleCharsが0以上なら、先頭からその文字数だけ(文字送り)。
	// 描いた文字数を返す(失敗は-1)
	int drawLines(Image &dst, const std::vector<TextLine> &lines, int x, int y, int lineSpacing, const Color &fg, int visibleChars = -1);
	// layoutLines + drawLines
	int drawWrapped(Image &dst, const char *text, int x, int y, int maxWidth, int lineSpacing, const Color &fg, int visibleChars = -1);
	int getLineHeight() const { return ::TTF_GetFontHeight(font_); }

private:
	int advanceOf(Uint32 codepoint) const; // 文字の送り幅(キャッシュする)

	TTF_Font * const font_;
	const float fontSize_;
	TTF_TextEngine *engine_ = nullptr; // drawTextが初めて呼ばれたときに作る
	TTF_Text *scratch_ = nullptr;
	mutable std::unordered_map<Uint32, int> advances_;
};

} // namespace SDL_

#endif // SDLTTFFONT_H_
