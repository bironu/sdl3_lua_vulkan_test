#include "sdl/SDLGamepad.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace SDL_
{

Gamepad::Gamepad(SDL_JoystickID id)
	: gamepad_(::SDL_OpenGamepad(id))
	, id_(id)
{
	if (!gamepad_) {
		::SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Gamepad open error! %s", ::SDL_GetError());
	}
	else {
		::SDL_Log("Gamepad: %s", name());
	}
}

Gamepad::~Gamepad()
{
	if (gamepad_) {
		::SDL_CloseGamepad(gamepad_);
	}
}

float Gamepad::axis(SDL_GamepadAxis axis) const
{
	if (!gamepad_) {
		return 0.0f;
	}
	const float value = static_cast<float>(::SDL_GetGamepadAxis(gamepad_, axis)) / 32767.0f;
	return std::clamp(std::fabs(value) < kDeadZone ? 0.0f : value, -1.0f, 1.0f);
}

void Gamepad::stick(SDL_GamepadAxis ax, SDL_GamepadAxis ay, float &x, float &y) const
{
	x = y = 0.0f;
	if (!gamepad_) {
		return;
	}
	const float rawX = static_cast<float>(::SDL_GetGamepadAxis(gamepad_, ax)) / 32767.0f;
	const float rawY = static_cast<float>(::SDL_GetGamepadAxis(gamepad_, ay)) / 32767.0f;
	const float length = std::sqrt(rawX * rawX + rawY * rawY);
	if (length < kDeadZone) {
		return;
	}
	// デッドゾーンの外を、0〜1に伸ばす(傾きの向きは保つ)
	const float scaled = std::min((length - kDeadZone) / (1.0f - kDeadZone), 1.0f);
	x = rawX / length * scaled;
	y = rawY / length * scaled;
}

} // namespace SDL_
