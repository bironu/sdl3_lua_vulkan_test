#include "ui/PadNavigator.h"
#include "ui/UiScript.h"
#include "resources/Resources.h"
#include "sdl/SDLGamepad.h"

namespace ui
{

void PadNavigator::poll(Resources &res, UiScript &script, uint32_t tick)
{
	constexpr float kStickThreshold = 0.5f;
	constexpr uint32_t kRepeatDelay = 500;
	constexpr uint32_t kRepeatInterval = 100;
	static const char *const kNames[4] = {"PadUp", "PadDown", "PadLeft", "PadRight"};
	const auto pad = res.getGamepad();
	bool now[4] = {false, false, false, false};
	if(pad){
		float x, y;
		pad->leftStick(x, y);
		now[0] = pad->button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kStickThreshold;
		now[1] = pad->button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kStickThreshold;
		now[2] = pad->button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kStickThreshold;
		now[3] = pad->button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kStickThreshold;
	}
	for(int i = 0; i < 4; ++i){
		Direction &direction = directions_[i];
		if(now[i] && !direction.pressed){
			direction.pressed = true;
			direction.nextRepeat = tick + kRepeatDelay;
			script.onKey(kNames[i], true);
		}
		else if(now[i] && tick >= direction.nextRepeat){
			direction.nextRepeat += kRepeatInterval;
			script.onKey(kNames[i], true); // 押し続けている間は、繰り返す
		}
		else if(!now[i] && direction.pressed){
			direction.pressed = false;
			script.onKey(kNames[i], false);
		}
	}
}

void PadNavigator::reset()
{
	for(auto &direction : directions_){
		direction = Direction();
	}
}

} // namespace ui
