#include "sdl/SDLRendererWindow.h"
#include "sdl/SDLTexture.h"
#include "sdl/SDLImage.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>

namespace SDL_
{

RendererWindow::RendererWindow(const char* title, int x, int y, int w, int h, Uint32 flags)
	: Window(title, x, y, w, h, flags)
	, renderer_(*this)
	, renderTexture_(nullptr)
	, backBuffer_()
{
	restoreRenderTexture();
}

RendererWindow::~RendererWindow() = default;

void RendererWindow::swap()
{
	{
		TextureLock lock(*renderTexture_);
		::SDL_BlitSurface(backBuffer_->get(), nullptr, lock.getSurface(), nullptr);
	}
	renderer_.clear();
	renderer_.copy(renderTexture_, nullptr, nullptr);
	renderer_.present();
}

void RendererWindow::restoreRenderTexture() {
	renderTexture_.reset();
	backBuffer_.reset();

	int w, h;
	// High-DPIに対応するため、"ピクセル単位"のサイズを取得
	if (!::SDL_GetWindowSizeInPixels(get(), &w, &h)) {
		::SDL_LogError(::SDL_LOG_CATEGORY_ERROR, "Failed to get window size in pixels: %s", ::SDL_GetError());
		exit(1); // エラー処理
	}

	// ウィンドウと一致する最適なピクセルフォーマットを取得
	SDL_PixelFormat format = ::SDL_GetWindowPixelFormat(get());
	if (format == SDL_PIXELFORMAT_UNKNOWN) {
		format = SDL_PIXELFORMAT_RGBA32; // フォールバック
	}

	// レンダリターゲット用のテクスチャを作成
	renderTexture_ = std::make_shared<SDL_::Texture>(renderer_, format, SDL_TEXTUREACCESS_STREAMING, w, h);
	backBuffer_ = std::make_shared<SDL_::Image>(w, h);
}

} // SDL_
