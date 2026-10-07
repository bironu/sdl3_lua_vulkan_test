#include "sdl/SDLJoystick.h"
#include "app/Application.h"
#include <algorithm>

namespace SDL_
{

Joystick::Joystick(SDL_JoystickID device_index)
	: joystick_(::SDL_OpenJoystick(device_index))
	, buttonPressInfoList_()
	, hatPressInfoMap_()
	, numButtons_()
{
	if (!isJoystick()) {
		::SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Joystick open error! %s", ::SDL_GetError());
	}
	else {
		numButtons_ = numButtons();
		buttonPressInfoList_.reset(new PressInfo[numButtons_]);
		const auto hatList = {SDL_HAT_LEFT, SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN};
		for (const auto hat : hatList) {
			hatPressInfoMap_.insert(std::make_pair(hat, PressInfo()));
		}
	}
}

Joystick::~Joystick()
{
	if (getAttached()) {
		::SDL_CloseJoystick(joystick_);
	}
}

void Joystick::updateJoyHatState(const SDL_JoyHatEvent &jhat)
{
}

void Joystick::updateHoyButtonState(const SDL_JoyButtonEvent &jbutton)
{
}

void Joystick::updateState(uint32_t tick)
{
	if (isJoystick()) {
		const auto hatList = {SDL_HAT_LEFT, SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN};
		const auto stateHat = getHat(0);
		for (const auto hat : hatList) {
			auto &hatPressInfo = hatPressInfoMap_[hat];
			hatPressInfo.prevPress = hatPressInfo.currPress;
			hatPressInfo.currPress = ((stateHat & hat) != 0);

			if (this->isHatPressTrigger(hat)) {
				hatPressInfo.tickPress = tick;
			}
			else if (this->isHatPressing(hat)) {
				if (((hatPressInfo.repeatCount == 0) && ((tick - hatPressInfo.tickPress) >= hatPressInfo.repeatDelay)) ||
						((hatPressInfo.repeatCount > 0) && ((tick - hatPressInfo.tickPress) >= hatPressInfo.repeatInterval))) {
					hatPressInfo.tickPress = tick;
					hatPressInfo.prevPress = false;
					++hatPressInfo.repeatCount;
				}
			}
			else if (this->isHatReleaseTrigger(hat)) {
				if (hatPressInfo.repeatCount > 0) {
					hatPressInfo.repeatCount = 0;
				}
			}
		}

		for (int i = 0; i < numButtons_; ++i) {
			auto &buttonPressInfo = buttonPressInfoList_[i];
			buttonPressInfo.prevPress = buttonPressInfo.currPress;
			buttonPressInfo.currPress = (getButton(i) != 0);

			if (this->isButtonPressTrigger(i)) {
				buttonPressInfo.tickPress = tick;
			}
			else if (this->isButtonPressing(i)) {
				if (((buttonPressInfo.repeatCount == 0) && ((tick - buttonPressInfo.tickPress) >= buttonPressInfo.repeatDelay)) ||
						((buttonPressInfo.repeatCount > 0) && ((tick - buttonPressInfo.tickPress) >= buttonPressInfo.repeatInterval))) {
					buttonPressInfo.tickPress = tick;
					buttonPressInfo.prevPress = false;
					++buttonPressInfo.repeatCount;
				}
			}
			else if (this->isButtonReleaseTrigger(i)) {
				if (buttonPressInfo.repeatCount > 0) {
					buttonPressInfo.repeatCount = 0;
				}
			}
		}
	}
}

bool Joystick::isHatPress(int hat) const
{
	return hatPressInfoMap_.at(hat).currPress;
}

bool Joystick::isHatPressing(int hat) const
{
	return hatPressInfoMap_.at(hat).prevPress && hatPressInfoMap_.at(hat).currPress;
}

bool Joystick::isHatPressTrigger(int hat) const
{
	return !hatPressInfoMap_.at(hat).prevPress && hatPressInfoMap_.at(hat).currPress;
}

bool Joystick::isHatReleaseTrigger(int hat) const
{
	return hatPressInfoMap_.at(hat).prevPress && !hatPressInfoMap_.at(hat).currPress;
}

uint32_t Joystick::getHatPressTick(int hat) const
{
	return hatPressInfoMap_.at(hat).tickPress;
}

void Joystick::setHatPressTick(int hat, uint32_t tick)
{
	hatPressInfoMap_[hat].tickPress = tick;
}

void Joystick::setHatRepeatEnable(int hat, uint32_t delay, uint32_t interval)
{
	hatPressInfoMap_[hat].repeatDelay = delay;
	hatPressInfoMap_[hat].repeatInterval = interval;
	hatPressInfoMap_[hat].repeatCount = 0;
}

void Joystick::setHatRepeatDisable(int hat)
{
	hatPressInfoMap_[hat].repeatCount = -1;
}

bool Joystick::isHatRepeat(int hat) const
{
	return (hatPressInfoMap_.at(hat).repeatCount >= 0);
}


bool Joystick::isButtonPress(int buttonIndex) const
{
	return buttonPressInfoList_[buttonIndex].currPress;
}

bool Joystick::isButtonPressing(int buttonIndex) const
{
	return buttonPressInfoList_[buttonIndex].prevPress && buttonPressInfoList_[buttonIndex].currPress;
}

bool Joystick::isButtonPressTrigger(int buttonIndex) const
{
	return !buttonPressInfoList_[buttonIndex].prevPress && buttonPressInfoList_[buttonIndex].currPress;
}

bool Joystick::isButtonReleaseTrigger(int buttonIndex) const
{
	return buttonPressInfoList_[buttonIndex].prevPress && !buttonPressInfoList_[buttonIndex].currPress;
}

uint32_t Joystick::getButtonPressTick(int buttonIndex) const
{
	return buttonPressInfoList_[buttonIndex].tickPress;
}

void Joystick::setButtonPressTick(int buttonIndex, uint32_t tick)
{
	buttonPressInfoList_[buttonIndex].tickPress = tick;
}

void Joystick::setButtonRepeatEnable(int buttonIndex, uint32_t delay, uint32_t interval)
{
	buttonPressInfoList_[buttonIndex].repeatCount = 0;
	buttonPressInfoList_[buttonIndex].repeatDelay = delay;
	buttonPressInfoList_[buttonIndex].repeatInterval = interval;
}

void Joystick::setButtonRepeatDisable(int buttonIndex)
{
	buttonPressInfoList_[buttonIndex].repeatCount = -1;
}

}
