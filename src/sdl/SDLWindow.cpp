#include "sdl/SDLWindow.h"
#include <SDL3/SDL_video.h>

namespace SDL_
{

Window::Window(const char* title, int x, int y, int w, int h, Uint32 flags)
	: window_(::SDL_CreateWindow(title, w, h, flags))
	, sceneHost_(*this)
{
	::SDL_SetWindowPosition(window_, x, y);
}

Window::~Window()
{
	if(isWindow()){
		::SDL_DestroyWindow(window_);
	}
}

} // SDL_
