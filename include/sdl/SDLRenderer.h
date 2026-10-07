#ifndef SDLRENDERER_H_
#define SDLRENDERER_H_

#include "sdl/SDLColor.h"
#include "geo/Rect.h"
#include "geo/FRect.h"
#include "geo/Vector2.h"
#include "misc/Uncopyable.h"
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_blendmode.h>
#include <memory>


namespace SDL_
{

class Window;
class Image;
class Texture;
class Renderer final
{
public:
	UNCOPYABLE(Renderer);
	explicit Renderer(const Image &surface);
	explicit Renderer(const Window &window);
	~Renderer();

	SDL_Renderer *get() const { return renderer_; }

	SDL_BlendMode getDrawBlendMode()
	{
		SDL_BlendMode blendMode;
		::SDL_GetRenderDrawBlendMode(get(), &blendMode);
		return blendMode;
	}
	const Color getDrawColor()
	{
		Uint8 r, g, b, a;
		::SDL_GetRenderDrawColor(get(), &r, &g, &b, &a);
		return Color(r, g, b, a);
	}
	//SDL_Texture* SDL_GetRenderTarget(SDL_Renderer* renderer);
	//bool SDL_GetRendererInfo(SDL_Renderer* renderer, SDL_RendererInfo* info);
	const geo::Sizei getOutputSize()
	{
		int w, h;
		::SDL_GetRenderOutputSize(get(),&w, &h);
		return {w, h};
	}
	void clear() { ::SDL_RenderClear(renderer_); }
	void copy(std::shared_ptr<Texture> texture, const FRect *srcrect, const FRect *dstrect);
	void copyEx(std::shared_ptr<Texture> texture, const FRect *srcrect, const FRect *dstrect, const double angle, const FPoint *center, const SDL_FlipMode flip);
	void drawLine(int x1, int y1, int x2, int y2) { ::SDL_RenderLine(get(), static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2)); }
	//bool SDL_RenderLines(SDL_Renderer* renderer, const SDL_FPoint* points, int count);
	void drawPoint(int x, int y) { ::SDL_RenderPoint(get(), static_cast<float>(x), static_cast<float>(y)); }
	//bool SDL_RenderPoints(SDL_Renderer* renderer, const SDL_FPoint* points, int count);
	void drawRect(const FRect &rect) { ::SDL_RenderRect(get(), &rect); }
	//bool SDL_RenderRects(SDL_Renderer* renderer, const SDL_FRect* rects, int count);
	void fillRect(const FRect &rect) { ::SDL_RenderFillRect(get(), &rect); }
	//bool SDL_RenderFillRects(SDL_Renderer* renderer, const SDL_FRect* rects, int count);
	const Rect getClipRect()
	{
		Rect clip;
		::SDL_GetRenderClipRect(get(), &clip);
		return clip;
	}
	const geo::Sizei getLogicalSize()
	{
		int w, h;
		SDL_RendererLogicalPresentation mode;
		::SDL_GetRenderLogicalPresentation(get(), &w, &h, &mode);
		return {w, h};
	}
	const geo::Vector2f getScale()
	{
		float w, h;
		::SDL_GetRenderScale(get(), &w, &h);
		return {w, h};
	}
	const Rect getViewport()
	{
		Rect rect;
		::SDL_GetRenderViewport(get(), &rect);
		return rect;
	}
	bool isClipEnabled()
	{
		return ::SDL_RenderClipEnabled(get());
	}
	void present() { ::SDL_RenderPresent(get()); }
	//bool SDL_RenderReadPixels(SDL_Renderer* renderer, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch);
	void setClipRect(const Rect &rect) {
		::SDL_SetRenderClipRect(get(), &rect);
	}
	void clearClipRect() {
		::SDL_SetRenderClipRect(get(), nullptr);
	}
	void setLogicalSize(int w, int h) { ::SDL_SetRenderLogicalPresentation(get(), w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX); }
	void setLogicalSize(const geo::Sizei &size) { ::SDL_SetRenderLogicalPresentation(get(), size.getWidth(), size.getHeight(), SDL_LOGICAL_PRESENTATION_LETTERBOX); }
	void setScale(float scaleX, float scaleY) { ::SDL_SetRenderScale(get(), scaleX, scaleY); }
	void setScale(const geo::Vector2f &scale) { ::SDL_SetRenderScale(get(), scale.getWidth(), scale.getHeight()); }
	void setViewport(const SDL_Rect &rect) { ::SDL_SetRenderViewport(get(), &rect); }
	void setDrawBlendMode(SDL_BlendMode blendMode) { ::SDL_SetRenderDrawBlendMode(get(), blendMode); }
	void setDrawColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a) { ::SDL_SetRenderDrawColor(get(), r, g, b, a); }
	void setDrawColor(const Color &color) { ::SDL_SetRenderDrawColor(get(), color.getRed(), color.getGreen(), color.getBlue(), color.getAlpha()); }
	void setTarget(std::shared_ptr<Texture> texture);

private:
	SDL_Renderer * const renderer_;
};

} // SDL_

#endif // SDLRENDERER_H_
