#if !defined(SDLRENDERERWINDOW_H_)
#define SDLRENDERERWINDOW_H_

#include "sdl/SDLWindow.h"
#include "sdl/SDLRenderer.h"
#include <memory>

namespace SDL_
{

class Texture;
class Image;

// SDL_Renderer + SDL_Surface(バックバッファ)で描画するウィンドウ
class RendererWindow : public Window
{
public:
	RendererWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
	~RendererWindow() override;

	SDL_::Renderer &getRenderer() { return renderer_; }
	std::shared_ptr<SDL_::Image> getBackBuffer() { return backBuffer_; }

	void swap() override;
	void onPixelSizeChanged() override { restoreRenderTexture(); }
	void restoreRenderTexture();

private:
	Renderer renderer_;
	std::shared_ptr<Texture> renderTexture_;
	std::shared_ptr<Image> backBuffer_;
};

} // SDL_

#endif // SDLRENDERERWINDOW_H_
