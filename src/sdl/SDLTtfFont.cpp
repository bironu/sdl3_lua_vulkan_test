#include "sdl/SDLTtfFont.h"
#include "sdl/SDLColor.h"
#include "sdl/SDLImage.h"
#include "geo/Vector2.h"
#include <SDL3/SDL_stdinc.h>
#include <algorithm>

namespace SDL_
{

namespace
{
// ---- 禁則処理の文字の分類 ----
// 行頭に置いてはいけない文字(句読点・閉じ括弧・長音・小書きのかななど)
bool isNoLineStart(Uint32 c)
{
	switch(c){
	case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0x30FB: case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF01: // 、。，．・：；？！
	case 0x309B: case 0x309C: case 0x30FD: case 0x30FE: case 0x309D: case 0x309E: case 0x3005: case 0x30FC: // ゛゜ヽヾゝゞ々ー
	case 0xFF09: case 0xFF5D: case 0x3015: case 0xFF3D: case 0x300D: case 0x300F: case 0x3011: case 0x3009: case 0x300B: case 0x3019: case 0x3017: case 0x301F: // 閉じ括弧
	case 0x2019: case 0x201D: case 0x2026: case 0x2025: case 0x2015: case 0x2014: // ’”…‥―—
	case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083: case 0x3085: case 0x3087: case 0x308E: // ぁぃぅぇぉっゃゅょゎ
	case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6: // ァィゥェォッャュョヮヵヶ
	case ',': case '.': case ':': case ';': case '?': case '!': case ')': case ']': case '}': case '%':
		return true;
	default:
		return false;
	}
}

// 行末に置いてはいけない文字(開き括弧)
bool isNoLineEnd(Uint32 c)
{
	switch(c){
	case 0xFF08: case 0xFF5B: case 0x3014: case 0xFF3B: case 0x300C: case 0x300E: case 0x3010: case 0x3008: case 0x300A: case 0x3018: case 0x3016: case 0x301D: // （｛〔［「『【〈《〘〖〝
	case 0x2018: case 0x201C: case '(': case '[': case '{':
		return true;
	default:
		return false;
	}
}

// 2つ並べて、途中で切ってはいけない文字(……、‥‥、――)
bool isInseparablePair(Uint32 a, Uint32 b)
{
	return a == b && (a == 0x2026 || a == 0x2025 || a == 0x2015 || a == 0x2014);
}

// 英数字の単語を作る文字(途中で切らない)
bool isWordChar(Uint32 c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '\'';
}

// 行末にぶら下げてよい句読点
bool isHangable(Uint32 c)
{
	return c == 0x3001 || c == 0x3002 || c == 0xFF0C || c == 0xFF0E;
}

// aとbの間で、行を分けてよいか
bool canBreakBetween(Uint32 a, Uint32 b)
{
	if(a == ' '){
		return true; // 空白の後ろは切れる
	}
	if(b == ' ' || isNoLineStart(b) || isNoLineEnd(a) || isInseparablePair(a, b) || (isWordChar(a) && isWordChar(b))){
		return false;
	}
	return true;
}
}

TtfFont::~TtfFont()
{
	if(scratch_){
		::TTF_DestroyText(scratch_);
	}
	if(engine_){
		::TTF_DestroySurfaceTextEngine(engine_);
	}
	if(font_){
		::TTF_CloseFont(font_);
	}
}

bool TtfFont::drawText(Image &dst, const char *text, int x, int y, const Color &fg)
{
	if(!font_ || !dst.isEnabled()){
		return false;
	}
	if(!engine_){
		engine_ = ::TTF_CreateSurfaceTextEngine();
	}
	if(engine_ && !scratch_){
		scratch_ = ::TTF_CreateText(engine_, font_, "", 0);
	}
	if(!scratch_){
		return false;
	}
	return ::TTF_SetTextString(scratch_, text, 0)
		&& ::TTF_SetTextColor(scratch_, fg.r, fg.g, fg.b, fg.a)
		&& ::TTF_DrawSurfaceText(scratch_, x, y, dst.get());
}

int TtfFont::advanceOf(Uint32 codepoint) const
{
	const auto found = advances_.find(codepoint);
	if(found != advances_.end()){
		return found->second;
	}
	int advance = 0;
	if(!::TTF_GetGlyphMetrics(font_, codepoint, nullptr, nullptr, nullptr, nullptr, &advance)){
		advance = static_cast<int>(fontSize_);
	}
	advances_[codepoint] = advance;
	return advance;
}

std::vector<TtfFont::TextLine> TtfFont::layoutLines(const char *text, int maxWidth, bool hangPunctuation) const
{
	struct Glyph
	{
		Uint32 cp;
		size_t offset, length;
		int advance;
	};
	std::vector<TextLine> lines;
	if(!font_ || !text){
		return lines;
	}
	// 1文字ずつに分ける(UTF-8のコードポイント)
	std::vector<Glyph> glyphs;
	{
		const char *p = text;
		size_t remaining = SDL_strlen(text);
		while(remaining > 0){
			const char *before = p;
			const Uint32 cp = SDL_StepUTF8(&p, &remaining);
			if(cp == 0){
				break;
			}
			glyphs.push_back({cp, static_cast<size_t>(before - text), static_cast<size_t>(p - before), cp == '\n' ? 0 : advanceOf(cp)});
		}
	}
	int charCounter = 0; // 改行を除いた文字の通し番号
	size_t lineStart = 0;
	int width = 0;
	long lastBreak = -1;
	const auto emit = [&](size_t from, size_t to){
		TextLine line;
		if(to > from){
			line.text.assign(text + glyphs[from].offset, glyphs[to - 1].offset + glyphs[to - 1].length - glyphs[from].offset);
		}
		for(size_t k = from; k < to; ++k){
			line.width += glyphs[k].advance;
		}
		line.firstChar = charCounter;
		line.charCount = static_cast<int>(to - from);
		charCounter += line.charCount;
		lines.push_back(std::move(line));
	};
	for(size_t i = 0; i < glyphs.size(); ++i){
		const Glyph &g = glyphs[i];
		if(g.cp == '\n'){
			emit(lineStart, i);
			lineStart = i + 1;
			width = 0;
			lastBreak = -1;
			continue;
		}
		if(i > lineStart && canBreakBetween(glyphs[i - 1].cp, g.cp)){
			lastBreak = static_cast<long>(i); // 直前までで、行を終えてよい
		}
		if(i > lineStart && g.cp != ' ' && width + g.advance > maxWidth){
			if(hangPunctuation && isHangable(g.cp) && width <= maxWidth){
				// ぶら下げ: 句読点は、行末からはみ出してよい
			}
			else{
				// 行を分ける。切れる位置が無ければ(長すぎる単語)、ここで強制的に
				const size_t at = (lastBreak > static_cast<long>(lineStart)) ? static_cast<size_t>(lastBreak) : i;
				emit(lineStart, at);
				lineStart = at;
				width = 0;
				for(size_t k = lineStart; k < i; ++k){
					width += glyphs[k].advance;
				}
				lastBreak = -1;
			}
		}
		width += g.advance;
	}
	if(lineStart < glyphs.size() || lines.empty()){
		emit(lineStart, glyphs.size());
	}
	return lines;
}

int TtfFont::drawLines(Image &dst, const std::vector<TextLine> &lines, int x, int y, int lineSpacing, const Color &fg, int visibleChars)
{
	const int step = getLineHeight() + lineSpacing;
	int drawn = 0;
	for(size_t row = 0; row < lines.size(); ++row){
		const TextLine &line = lines[row];
		int count = line.charCount;
		if(visibleChars >= 0){
			count = std::clamp(visibleChars - line.firstChar, 0, line.charCount);
		}
		if(count <= 0){
			continue;
		}
		// 先頭からcount文字ぶんの文字列
		const char *p = line.text.c_str();
		size_t remaining = line.text.size();
		for(int k = 0; k < count && remaining > 0; ++k){
			SDL_StepUTF8(&p, &remaining);
		}
		const std::string part(line.text.c_str(), static_cast<size_t>(p - line.text.c_str()));
		if(!drawText(dst, part.c_str(), x, y + static_cast<int>(row) * step, fg)){
			return -1;
		}
		drawn += count;
	}
	return drawn;
}

int TtfFont::drawWrapped(Image &dst, const char *text, int x, int y, int maxWidth, int lineSpacing, const Color &fg, int visibleChars)
{
	return drawLines(dst, layoutLines(text, maxWidth), x, y, lineSpacing, fg, visibleChars);
}

bool TtfFont::getTextSize(const char* text, geo::Sizei *size) {
	bool result = false;
	if(size){
		int w, h;
		result = ::TTF_GetStringSize(font_, text, 0, &w, &h);
		if(result){
			size->setWidth(w);
			size->setHeight(h);
		}
	}
	return result;
}

std::shared_ptr<SDL_::Image> TtfFont::renderSolidText(const char* text, const SDL_::Color& fg)
{
	std::shared_ptr<SDL_::Image> result;
	SDL_Surface *surface = ::TTF_RenderText_Solid(font_, text, 0, fg);
	if(surface){
		result.reset(new SDL_::Image(surface));
	}
	return result;
}

std::shared_ptr<SDL_::Image> TtfFont::renderShadedText(const char* text, const SDL_::Color& fg, const SDL_::Color& bg)
{
	std::shared_ptr<SDL_::Image> result;
	SDL_Surface *surface = ::TTF_RenderText_Shaded(font_, text, 0, fg, bg);
	if(surface){
		result.reset(new SDL_::Image(surface));
	}
	return result;
}

std::shared_ptr<SDL_::Image> TtfFont::renderBlendedText(const char* text, const SDL_::Color& fg)
{
	std::shared_ptr<SDL_::Image> result;
	SDL_Surface *surface = ::TTF_RenderText_Blended(font_, text, 0, fg);
	if(surface){
		result.reset(new SDL_::Image(surface));
	}
	return result;
}

} // namespace SDL_
