#if !defined(SDLIMAGE_H_)
#define SDLIMAGE_H_

#include "geo/Rect.h"
#include "geo/Point.h"
#include "geo/Vector2.h"
#include "sdl/SDLColor.h"
#include "misc/Uncopyable.h"
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <memory>
#include <cassert>

namespace SDL_
{

class Image
{
public:
	UNCOPYABLE(Image);
	explicit Image(SDL_Surface *surface)
		: surface_(surface)
	{
	}
	Image(int width, int height);
	explicit Image(const char * const file, bool isColorKey = false);
	// メモリ上の画像ファイル(PNG/JPEG等)を読む。読めなければnullptr。ignoreAlphaなら、アルファを全部不透明(255)にする
	static std::shared_ptr<Image> fromMemory(const void *data, size_t size, bool ignoreAlpha = false);
	~Image();

	SDL_Surface *get() const { return surface_;}

	operator bool(){ return surface_ != nullptr; }

	bool lock()
	{
		assert(isEnabled());
		if(!isLocked()){
			return ::SDL_LockSurface(get());
		}
		else{
			return true;
		}
	}
	void unlock()
	{
		assert(isEnabled());
		if(isLocked()){
			::SDL_UnlockSurface(get());
		}
	}
	bool blitScaled(std::shared_ptr<Image> src, const Rect *srcrect, Rect *dstrect, SDL_ScaleMode scaleMode = SDL_SCALEMODE_NEAREST)
	{
		assert(isEnabled() && src->isEnabled());
		return ::SDL_BlitSurfaceScaled(src->get(), srcrect, get(), dstrect, scaleMode);
	}
	bool blit(std::shared_ptr<Image> src, const Rect &srcrect, Sint16 nXDest, Sint16 nYDest)
	{
		Rect dstrect{nXDest, nYDest, 0, 0};
		return ::SDL_BlitSurface(src->get(), &srcrect, get(), &dstrect);
	}
	bool blit(std::shared_ptr<Image> src, Sint16 nXDest, Sint16 nYDest)
	{
		Rect dstrect{nXDest, nYDest, 0, 0};
		return ::SDL_BlitSurface(src->get(), nullptr, get(), &dstrect);
	}
	bool fillRect(const Rect &rect, const Uint32 color)
	{
		return ::SDL_FillSurfaceRect(get(), &rect, color);
	}
	bool fillRect(const Rect &rect, const Color &color)
	{
		return fillRect(rect, mapRGBA(color.getRed(), color.getGreen(), color.getBlue(), color.getAlpha()));
	}
	bool fillRect(const Uint32 color)
	{
		assert(isEnabled());
		return ::SDL_FillSurfaceRect(get(), nullptr, color);
	}
	bool fillRect(const Color &color)
	{
		assert(isEnabled());
		return fillRect(mapRGBA(color.getRed(), color.getGreen(), color.getBlue(), color.getAlpha()));
	}

	const Rect getClipRect()
	{
		Rect result;
		::SDL_GetSurfaceClipRect(get(), &result);
		return result;
	}
	Uint32 getColorKey()
	{
		Uint32 result;
		::SDL_GetSurfaceColorKey(get(), &result);
		return result;
	}
	const Color getColorMod() {
		Uint8 r, g, b, a;
		::SDL_GetSurfaceAlphaMod(get(), &a);
		::SDL_GetSurfaceColorMod(get(), &r, &g, &b);
		return Color(r, g, b, a);
	}
	SDL_BlendMode getBlendMode()
	{
		SDL_BlendMode blendMode;
		::SDL_GetSurfaceBlendMode(get(), &blendMode);
		return blendMode;
	}
	bool saveBMP(const char *file) { return ::SDL_SaveBMP(get(), file); }
	void setClipRect(const Rect &rect) { ::SDL_SetSurfaceClipRect(get(), &rect); }
	void clearClipRect() { ::SDL_SetSurfaceClipRect(get(), nullptr); }
	void setColorKey(Uint32 color) { ::SDL_SetSurfaceColorKey(get(), true, color); }
	void setColorKey(const Color &color) { ::SDL_SetSurfaceColorKey(get(), true, mapRGB(color.getRed(), color.getGreen(), color.getBlue())); }
	void clearColorKey() { ::SDL_SetSurfaceColorKey(get(), false, 0); }
	void setAlphaMod(Uint8 alpha) { ::SDL_SetSurfaceAlphaMod(get(), alpha); }
	void setBlendMode(SDL_BlendMode blendMode) { ::SDL_SetSurfaceBlendMode(get(), blendMode); }
	void setColorMod(Uint8 r, Uint8 g, Uint8 b) { ::SDL_SetSurfaceColorMod(get(), r, g, b); }
	//SDL_SetSurfacePalette
	//SDL_SetSurfaceRLE

	Uint32 mapRGB(Uint8 r, Uint8 g, Uint8 b){ assert(isEnabled()); return ::SDL_MapRGB(::SDL_GetPixelFormatDetails(get()->format), nullptr, r, g, b);}
	Uint32 mapRGBA(Uint8 r, Uint8 g, Uint8 b, Uint8 a){ assert(isEnabled()); return ::SDL_MapRGBA(::SDL_GetPixelFormatDetails(get()->format), nullptr, r, g, b, a);}
	bool isEnabled() const { return get() !=  nullptr;}
	bool isLocked() const { return SDL_MUSTLOCK(get()) == 0;}
	int getHeight() const { assert(isEnabled()); return get()->h;}
	int getWidth() const { assert(isEnabled()); return get()->w;}
	const geo::Sizei getSize() const { return {getWidth(), getHeight()}; }
	SDL_PixelFormat GetPixelFormat() const { assert(isEnabled()); return get()->format;}
	const Uint32 getFlags() const { assert(isEnabled()); return get()->flags;}
	const void *getPixels() const { assert(isEnabled()); return get()->pixels;}
    int getPitch() const { assert(isEnabled()); return get()->pitch;}

	bool fillRoundedBox( int xo, int yo, int w, int h, int r, Uint32 color );

private:
	SDL_Surface * const surface_;
};

// 大きなシート画像(sheet)の一部(rect)を指す軽量な参照。スプライトシートの
// 1コマ分を、都度コピーを作らずにblit(sheet, rect, x, y)で直接描画するために使う
struct SubImage
{
	std::shared_ptr<Image> sheet;
	Rect rect;
};

} // SDL_

#endif // SDLIMAGE_H_
