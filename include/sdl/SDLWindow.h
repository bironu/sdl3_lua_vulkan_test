#if !defined(SDLWINDOW_H_)
#define SDLWINDOW_H_

#include "misc/Uncopyable.h"
#include "scene/SceneHost.h"
#include "geo/Point.h"
#include "geo/Vector2.h"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_properties.h>
#include <map>
#include <memory>

namespace SDL_
{

// ウィンドウの基底。描画方式(SDL_Renderer/Vulkan等)ごとに派生し、swap()を実装する。
// Scene管理(SceneHost)は基底が1つ所有する
class Window
{
public:
	UNCOPYABLE(Window);
	Window(const char* title, int x, int y, int w, int h, Uint32 flags);
	virtual ~Window();

	bool isWindow() const { return window_; }
	SDL_Window *get() const { return window_; }

	//float getBrightness() const { return ::SDL_GetWindowBrightness(window_); }
	// SDL3ではSDL_Get/SetWindowDataが廃止され、Properties API経由になった
	void* getUsaerData(const char* name) const { return ::SDL_GetPointerProperty(::SDL_GetWindowProperties(window_), name, nullptr); }
	SDL_DisplayID getDisplayIndex() const { return ::SDL_GetDisplayForWindow(window_); }
	//int getDisplayMode(SDL_DisplayMode* mode) const { return ::SDL_GetWindowDisplayMode(window_, mode); }
	SDL_WindowFlags getWindowFlag() const { return ::SDL_GetWindowFlags(window_); }
	//SDL_Window* getWindowFromID(Uint32 id) const { return ::SDL_GetWindowFromID(id); }
	//int getGammaRamp(Uint16* red, Uint16* green, Uint16* blue) const { return ::SDL_GetWindowGammaRamp(window_, red, green, blue); }
	//bool getGrab() const { return ::SDL_GetWindowGrab(window_); }
	Uint32 getWindowId() const { return ::SDL_GetWindowID(window_); }
	void getMaximumSize(int* w, int* h) const { ::SDL_GetWindowMaximumSize(window_, w, h); }
	void getMinimumSize(int* w, int* h) const { ::SDL_GetWindowMinimumSize(window_, w, h); }
	SDL_PixelFormat getPixelFormat() const { return ::SDL_GetWindowPixelFormat(window_); }
	const Point getPosition() const
	{
		int x, y;
		::SDL_GetWindowPosition(window_, &x, &y);
		return {x, y};
	}
	const geo::Sizei getSize() const
	{
		int w, h;
		::SDL_GetWindowSize(window_, &w, &h);
		return {w, h};
	}
	//SDL_Surface* SDL_GetWindowSurface(SDL_Window* window);
	const char *getTitle() const { return ::SDL_GetWindowTitle(window_); }
	//SDL_bool SDL_GetWindowWMInfo(SDL_Window* window, SDL_SysWMinfo* info);
	void hide() { ::SDL_HideWindow(window_); }
	void maximize() { ::SDL_MaximizeWindow(window_); }
	void minimize() { ::SDL_MinimizeWindow(window_); }
	void raise() { ::SDL_RaiseWindow(window_); }
	void restore() { ::SDL_RestoreWindow(window_); }
	void setBordered(bool bordered) { ::SDL_SetWindowBordered(window_, bordered); }
	//int SDL_SetWindowBrightness(SDL_Window* window, float brightness);
	bool setUserData(const char* name, void* userdata) { return ::SDL_SetPointerProperty(::SDL_GetWindowProperties(window_), name, userdata); }
	//int SDL_SetWindowDisplayMode(SDL_Window* window, const SDL_DisplayMode* mode);
	bool setFullscreen(bool fullscreen) { return ::SDL_SetWindowFullscreen(window_, fullscreen); }
	//int SDL_SetWindowGammaRamp(SDL_Window* window, const Uint16* red, const Uint16* green, const Uint16* blue);
	//void SDL_SetWindowGrab(SDL_Window* window, SDL_bool grabbed);
	//int SDL_SetWindowHitTest(SDL_Window* window, SDL_HitTest callback, void* callback_data);
	//void setIcon(Surface &surface) { ::SDL_SetWindowIcon(window_, surface.get()); }
	void setMaximumSize(int max_w, int max_h) { ::SDL_SetWindowMaximumSize(window_, max_w, max_h); }
	void setMinimumSize(int min_w, int min_h) { ::SDL_SetWindowMinimumSize(window_, min_w, min_h); }
	void setPosition(int x, int y) { ::SDL_SetWindowPosition(window_, x, y); }
	void setSize(int w, int h) { ::SDL_SetWindowSize(window_, w, h); }
	void setTitle(const char *title) { ::SDL_SetWindowTitle(window_, title); }
	//bool SDL_ShowMessageBox(const SDL_MessageBoxData* messageboxdata, int* buttonid);
	bool showSimpleMessageBox(Uint32 flags, const char* title, const char* message) { return ::SDL_ShowSimpleMessageBox(flags, title, message, window_); }
	void show() { ::SDL_ShowWindow(window_); }
	//int updateSurface() { ::SDL_UpdateWindowSurface(window_); }
	//int updateSurfaceRects(const SDL_Rect* rects, int numrects) { ::SDL_UpdateWindowSurfaceRects(window_, rects, numrects); }
	SceneHost &getSceneHost() { return sceneHost_; }

	// 画面を更新する(描画方式ごとに実装)
	virtual void swap() = 0;
	// ピクセルサイズ変更時に呼ばれる(High-DPI/リサイズ。必要なら再生成する)
	virtual void onPixelSizeChanged() {}
    void requestUpdate() {
        SDL_Event event = {SDL_EVENT_WINDOW_EXPOSED};
        event.window.windowID = getWindowId();
        SDL_PushEvent(&event);
    }

private:
	SDL_Window * const window_;
	SceneHost sceneHost_;
};

} // SDL_

#endif // SDLWINDOW_H_
