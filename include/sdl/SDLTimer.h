#if !defined(SDLTIMER_H_)
#define SDLTIMER_H_

#include "misc/Uncopyable.h"
#include <SDL3/SDL_timer.h>
#include <functional>

namespace SDL_
{

// SDL_AddTimer/SDL_RemoveTimerのラッパー。
// コールバックはstd::functionで受け取る。戻り値は次回のインターバル(ms)で、
// 0を返すとタイマーは停止する。
class Timer
{
public:
	using Callback = std::function<Uint32(Uint32 interval)>;

	UNCOPYABLE(Timer);
	Timer(Uint32 intervalMs, Callback callback);
	~Timer();

	SDL_TimerID id() const { return id_; }

private:
	static Uint32 SDLCALL trampoline(void *userdata, SDL_TimerID timerID, Uint32 interval);

	Callback callback_;
	SDL_TimerID id_;
};

} // namespace SDL_

#endif // SDLTIMER_H_
