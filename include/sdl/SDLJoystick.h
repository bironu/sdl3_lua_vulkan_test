#ifndef JOYSTICK_H_
#define JOYSTICK_H_

#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <map>
#include <vector>
#include <tuple>
#include <memory>

namespace SDL_
{

class Joystick {
public:
	explicit Joystick(SDL_JoystickID);
	~Joystick();

	bool isJoystick() const { return joystick_ != nullptr; }

	static int num() {
		int count = 0;
		SDL_JoystickID *ids = ::SDL_GetJoysticks(&count);
		::SDL_free(ids);
		return count;
	}
	static void lock() { ::SDL_LockJoysticks(); }
	static void unlock() { ::SDL_UnlockJoysticks(); }
	static void update() { ::SDL_UpdateJoysticks(); }

	//extern SDL_DECLSPEC SDL_Joystick *SDLCALL SDL_OpenJoystick(SDL_JoystickID instance_id);
	const char *getName() const { return ::SDL_GetJoystickName(joystick_); }
	SDL_GUID getGUID() const { return ::SDL_GetJoystickGUID(joystick_); }
	uint16_t getVendor() const { return ::SDL_GetJoystickVendor(joystick_); }
	uint16_t getProduct() const { return ::SDL_GetJoystickProduct(joystick_); }
	uint16_t getProductVersion() const { return ::SDL_GetJoystickProductVersion(joystick_); }
	SDL_JoystickType getType() const { return ::SDL_GetJoystickType(joystick_); }
	//extern SDL_DECLSPEC void SDLCALL SDL_GetJoystickGUIDInfo(SDL_GUID guid, ...);
	bool getAttached() const { return ::SDL_JoystickConnected(joystick_); }
	SDL_JoystickID getInstanceID() const { return ::SDL_GetJoystickID(joystick_); }
	int numAxes() const { return ::SDL_GetNumJoystickAxes(joystick_); }
	int numBalls() const { return ::SDL_GetNumJoystickBalls(joystick_); }
	int numHats() const { return ::SDL_GetNumJoystickHats(joystick_); }
	int numButtons() const { return ::SDL_GetNumJoystickButtons(joystick_); }

	bool isEventEnable() const { return ::SDL_JoystickEventsEnabled(); }
	void setEventEnable() { ::SDL_SetJoystickEventsEnabled(true); }
	void setEventDisable() { ::SDL_SetJoystickEventsEnabled(false); }

	int16_t getAxis(int axis) const { return ::SDL_GetJoystickAxis(joystick_, axis); }
	int16_t getAxisInitialState(int axis) const {
		Sint16 state;
		if (!::SDL_GetJoystickAxisInitialState(joystick_, axis, &state)) {
			::SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Joystick AxisInitialState error [%s]", ::SDL_GetError());
			state = 0;
		}
		return state;
	}

	uint8_t getHat(int hat) const { return ::SDL_GetJoystickHat(joystick_, hat); }
	bool getBall(int ball, int *dx, int *dy) const { return ::SDL_GetJoystickBall(joystick_, ball, dx, dy); }
	uint8_t getButton(int button) const { return ::SDL_GetJoystickButton(joystick_, button); }
	SDL_PowerState getCurrentPowerLevel() const { return ::SDL_GetJoystickPowerInfo(joystick_, nullptr); }

	void updateJoyHatState(const SDL_JoyHatEvent &);
	void updateHoyButtonState(const SDL_JoyButtonEvent &);
	void updateState(uint32_t);

	bool isHatPress(int hat) const;
	bool isHatPressing(int hat) const;
	bool isHatPressTrigger(int hat) const;
	bool isHatReleaseTrigger(int hat) const;
	uint32_t getHatPressTick(int hat) const;
	void setHatPressTick(int hat, uint32_t tick);
	void setHatRepeatEnable(int hat, uint32_t delay, uint32_t interval);
	void setHatRepeatDisable(int hat);
	bool isHatRepeat(int hat) const;

	bool isLeftPress() const { return isHatPress(SDL_HAT_LEFT); }
	bool isLeftPressing() const { return isHatPressing(SDL_HAT_LEFT); }
	bool isLeftPressTrigger() const { return isHatPressTrigger(SDL_HAT_LEFT); }
	bool isLeftReleaseTrigger() const { return isHatReleaseTrigger(SDL_HAT_LEFT); }
	uint32_t getLeftPressTick() const { return getHatPressTick(SDL_HAT_LEFT); }
	void setLeftPressTick(uint32_t tick) { setHatPressTick(SDL_HAT_LEFT, tick); }
	void setLeftRepeatEnable(uint32_t delay, uint32_t interval) { setHatRepeatEnable(SDL_HAT_LEFT, delay, interval); }
	void setLeftRepeatDisable() { setHatRepeatDisable(SDL_HAT_LEFT); }
	bool isLeftRepeat() const { return isHatRepeat(SDL_HAT_LEFT); }

	bool isUpPress() const { return isHatPress(SDL_HAT_UP); }
	bool isUpPressing() const { return isHatPressing(SDL_HAT_UP); }
	bool isUpPressTrigger() const { return isHatPressTrigger(SDL_HAT_UP); }
	bool isUpReleaseTrigger() const { return isHatReleaseTrigger(SDL_HAT_UP); }
	uint32_t getUpPressTick() const { return getHatPressTick(SDL_HAT_UP); }
	void setUpPressTick(uint32_t tick) { setHatPressTick(SDL_HAT_UP, tick); }
	void setUpRepeatEnable(uint32_t delay, uint32_t interval) { setHatRepeatEnable(SDL_HAT_UP, delay, interval); }
	void setUpRepeatDisable() { setHatRepeatDisable(SDL_HAT_UP); }
	bool isUpRepeat() const { return isHatRepeat(SDL_HAT_UP); }

	bool isRightPress() const { return isHatPress(SDL_HAT_RIGHT); }
	bool isRightPressing() const { return isHatPressing(SDL_HAT_RIGHT); }
	bool isRightPressTrigger() const { return isHatPressTrigger(SDL_HAT_RIGHT); }
	bool isRightReleaseTrigger() const { return isHatReleaseTrigger(SDL_HAT_RIGHT); }
	uint32_t getRightPressTick() const { return getHatPressTick(SDL_HAT_RIGHT); }
	void setRightPressTick(uint32_t tick) { setHatPressTick(SDL_HAT_RIGHT, tick); }
	void setRightRepeatEnable(uint32_t delay, uint32_t interval) { setHatRepeatEnable(SDL_HAT_RIGHT, delay, interval); }
	void setRightRepeatDisable() { setHatRepeatDisable(SDL_HAT_RIGHT); }
	bool isRightRepeat() const { return isHatRepeat(SDL_HAT_RIGHT); }

	bool isDownPress() const { return isHatPress(SDL_HAT_DOWN); }
	bool isDownPressing() const { return isHatPressing(SDL_HAT_DOWN); }
	bool isDownPressTrigger() const { return isHatPressTrigger(SDL_HAT_DOWN); }
	bool isDownReleaseTrigger() const { return isHatReleaseTrigger(SDL_HAT_DOWN); }
	uint32_t getDownPressTick() const { return getHatPressTick(SDL_HAT_DOWN); }
	void setDownPressTick(uint32_t tick) { setHatPressTick(SDL_HAT_DOWN, tick); }
	void setDownRepeatEnable(uint32_t delay, uint32_t interval) { setHatRepeatEnable(SDL_HAT_DOWN, delay, interval); }
	void setDownRepeatDisable() { setHatRepeatDisable(SDL_HAT_DOWN); }
	bool isDownRepeat() const { return isHatRepeat(SDL_HAT_DOWN); }

	bool isButtonPress(int buttonIndex) const;
	bool isButtonPressing(int buttonIndex) const;
	bool isButtonPressTrigger(int buttonIndex) const;
	bool isButtonReleaseTrigger(int buttonIndex) const;
	uint32_t getButtonPressTick(int buttonIndex) const;
	void setButtonPressTick(int buttonIndex, uint32_t tick);
	void setButtonRepeatEnable(int buttonIndex, uint32_t delay, uint32_t interval);
	void setButtonRepeatDisable(int buttonIndex);

private:
	struct PressInfo
	{
		PressInfo()
			: tickPress()
			, repeatDelay()
			, repeatInterval()
			, repeatCount(-1)
			, prevPress()
			, currPress()
		{
		}
		uint32_t tickPress;
		uint32_t repeatDelay;
		uint32_t repeatInterval;
		int repeatCount;
		bool prevPress;
		bool currPress;
	};

	SDL_Joystick * const joystick_;
	std::unique_ptr<PressInfo[]> buttonPressInfoList_;
	std::map<int, PressInfo> hatPressInfoMap_;
	int numButtons_;
};

}

#endif // JOYSTICK_H_
