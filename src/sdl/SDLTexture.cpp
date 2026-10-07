#include "sdl/SDLTexture.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLRenderer.h"

namespace SDL_
{

Texture::Texture(const Renderer &renderer, const Image &surface)
	: texture_(::SDL_CreateTextureFromSurface(renderer.get(), surface.get()))
	, width_(surface.getWidth())
	, height_(surface.getHeight())
{
}


Texture::Texture(const Renderer &renderer, SDL_PixelFormat format, SDL_TextureAccess access, int w, int h)
	: texture_(::SDL_CreateTexture(renderer.get(), format, access, w, h))
	, width_(w)
	, height_(h)
{
}

Texture::~Texture()
{
	if (texture_) {
		::SDL_DestroyTexture(texture_);
	}
}

TextureLock::TextureLock(Texture &texture)
    : texture_(texture.get())
    , surface_(nullptr)
{
    if (!::SDL_LockTextureToSurface(texture_, nullptr, &surface_)) {
        // ロックに失敗した場合のエラーハンドリング
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to lock texture: %s", SDL_GetError());
    }
}

TextureLock::~TextureLock()
{
    ::SDL_UnlockTexture(texture_);
}

} // SDL_
