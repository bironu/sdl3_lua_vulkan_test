#include "sdl/SDLTimer.h"

namespace SDL_
{

Timer::Timer(Uint32 intervalMs, Callback callback)
	: callback_(std::move(callback))
	, id_(::SDL_AddTimer(intervalMs, &Timer::trampoline, this))
{
}

Timer::~Timer()
{
	// コールバックはthisをuserdataとして受け取っているため、
	// thisの破棄が始まる前に必ずタイマーを停止させる。
	::SDL_RemoveTimer(id_);
}

Uint32 Timer::trampoline(void *userdata, SDL_TimerID /*timerID*/, Uint32 interval)
{
	return static_cast<Timer *>(userdata)->callback_(interval);
}

} // namespace SDL_
