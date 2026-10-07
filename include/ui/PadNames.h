#if !defined(UI_PADNAMES_H_)
#define UI_PADNAMES_H_

#include <SDL3/SDL_gamepad.h>
#include <string>

namespace ui
{

// ゲームパッドのボタンの、Luaでの名前(キーボードのキー名と同じ形で、onKey/input.isDownに使う)
// 十字キー: PadUp/PadDown/PadLeft/PadRight、PadA(下=決定)/PadB(右=キャンセル)/PadX(左)/PadY(上)、
// PadStart/PadBack/PadLB/PadRB/PadL3/PadR3。名前の無いボタンは空文字列
inline const char *padButtonName(SDL_GamepadButton button)
{
	switch(button){
	case SDL_GAMEPAD_BUTTON_DPAD_UP: return "PadUp";
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "PadDown";
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "PadLeft";
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "PadRight";
	case SDL_GAMEPAD_BUTTON_SOUTH: return "PadA";
	case SDL_GAMEPAD_BUTTON_EAST: return "PadB";
	case SDL_GAMEPAD_BUTTON_WEST: return "PadX";
	case SDL_GAMEPAD_BUTTON_NORTH: return "PadY";
	case SDL_GAMEPAD_BUTTON_START: return "PadStart";
	case SDL_GAMEPAD_BUTTON_BACK: return "PadBack";
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "PadLB";
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "PadRB";
	case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "PadL3";
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "PadR3";
	default: return "";
	}
}

inline bool padButtonFromName(const std::string &name, SDL_GamepadButton &button)
{
	for(int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i){
		const auto candidate = static_cast<SDL_GamepadButton>(i);
		if(name == padButtonName(candidate)){
			button = candidate;
			return true;
		}
	}
	return false;
}

} // namespace ui

#endif // UI_PADNAMES_H_
