#ifndef SDL_GAMEPAD_H_
#define SDL_GAMEPAD_H_

#include "misc/Uncopyable.h"
#include <SDL3/SDL_gamepad.h>

namespace SDL_
{

// ゲームパッド(SDLの標準配置に直されたコントローラ。左右のスティック・十字キー・ABXYなどが、機種によらず同じ名前で使える)のRAIIラッパー。
// スティックはイベントが来ないので、毎フレーム値を読む(axis/leftStick/rightStick)。ボタンはイベントでも、読んでもよい
class Gamepad
{
public:
	UNCOPYABLE(Gamepad);
	explicit Gamepad(SDL_JoystickID id);
	~Gamepad();

	bool isOpen() const { return gamepad_ != nullptr; }
	SDL_JoystickID instanceId() const { return id_; }
	const char *name() const { return gamepad_ ? ::SDL_GetGamepadName(gamepad_) : ""; }

	// 軸の値(-1〜1。トリガーは0〜1)。中心付近(デッドゾーン)は0
	float axis(SDL_GamepadAxis axis) const;
	// スティックの傾き(x: 右が+、y: 下が+。長さは0〜1)。デッドゾーンを円で取り、その外を0〜1に伸ばす(斜めでも傾きが自然)
	void leftStick(float &x, float &y) const { stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, x, y); }
	void rightStick(float &x, float &y) const { stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, x, y); }
	bool button(SDL_GamepadButton button) const { return gamepad_ && ::SDL_GetGamepadButton(gamepad_, button); }

private:
	static constexpr float kDeadZone = 0.18f;
	void stick(SDL_GamepadAxis ax, SDL_GamepadAxis ay, float &x, float &y) const;

	SDL_Gamepad *gamepad_;
	SDL_JoystickID id_;
};

} // namespace SDL_

#endif // SDL_GAMEPAD_H_
