#include "sdl/SDLRenderer.h"
#include "geo/Rect.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLTexture.h"
#include "sdl/SDLWindow.h"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_render.h>

namespace SDL_
{

Renderer::Renderer(const Image &surface)
	: renderer_(::SDL_CreateSoftwareRenderer(surface.get()))
{

}

Renderer::Renderer(const Window &window)
	: renderer_(::SDL_CreateRenderer(window.get(), nullptr))
{
	::SDL_SetRenderVSync(renderer_, 1);
}

Renderer::~Renderer()
{
	if(renderer_){
		::SDL_DestroyRenderer(renderer_);
	}
}

void Renderer::copy(std::shared_ptr<Texture> texture, const FRect *srcrect, const FRect *dstrect)
{
	::SDL_RenderTexture(get(), texture->get(), srcrect, dstrect);
}

void Renderer::copyEx(std::shared_ptr<Texture> texture, const FRect *srcrect, const FRect *dstrect, const double angle, const FPoint *center, const SDL_FlipMode flip)
{
	::SDL_RenderTextureRotated(get(), texture->get(), srcrect, dstrect, angle, center, flip);
}

void Renderer::setTarget(std::shared_ptr<Texture> texture)
{
    ::SDL_SetRenderTarget(get(), texture->get());
}


} // SDL_
