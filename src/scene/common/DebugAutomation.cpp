#include "scene/common/DebugAutomation.h"
#include "ui/UiScript.h"
#include <SDL3/SDL_stdinc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace game
{

std::vector<std::pair<std::string, int>> parseTimedList(const char *text)
{
	std::vector<std::pair<std::string, int>> list;
	const std::string all(text);
	for(size_t start = 0; start < all.size();){
		size_t end = all.find(',', start);
		end = end == std::string::npos ? all.size() : end;
		const std::string spec = all.substr(start, end - start);
		const auto at = spec.find('@');
		if(at != std::string::npos){
			list.emplace_back(spec.substr(0, at), std::atoi(spec.c_str() + at + 1));
		}
		start = end + 1;
	}
	return list;
}

DebugAutomation DebugAutomation::fromEnv()
{
	DebugAutomation a;
	if(const char *s = SDL_getenv("VULKAN_AUTOKEY")){
		a.keys_ = parseTimedList(s);
	}
	if(const char *s = SDL_getenv("VULKAN_AUTOACTION")){
		a.actions_ = parseTimedList(s);
	}
	if(const char *s = SDL_getenv("VULKAN_AUTOPAUSE")){
		a.pauseAt_ = std::atoi(s);
	}
	if(const char *s = SDL_getenv("VULKAN_AUTORELOAD")){
		a.reloadAt_ = std::atoi(s);
	}
	if(const char *s = SDL_getenv("VULKAN_AUTOMOUSE")){
		Mouse m;
		if(std::sscanf(s, "%f,%f@%d", &m.x, &m.y, &m.at) == 3){
			m.click = std::strstr(s, ",click") != nullptr;
			a.mouse_ = m;
		}
	}
	if(const char *s = SDL_getenv("VULKAN_AUTOWALK")){
		a.hasWalk_ = true;
		a.walkMode_ = s[0];
	}
	if(const char *s = SDL_getenv("VULKAN_AUTORUN")){
		a.hasRun_ = true;
		a.runMode_ = s[0];
	}
	if(const char *s = SDL_getenv("VULKAN_START")){
		float x = 0.0f, z = 0.0f;
		if(std::sscanf(s, "%f,%f", &x, &z) == 2){
			a.start_ = std::make_pair(x, z);
		}
	}
	if(const char *s = SDL_getenv("VULKAN_PITCH")){
		a.pitch_ = static_cast<float>(SDL_atof(s));
	}
	if(const char *s = SDL_getenv("VULKAN_YAW")){
		a.yaw_ = static_cast<float>(SDL_atof(s));
	}
	return a;
}

const std::string *DebugAutomation::nextDue(const std::vector<std::pair<std::string, int>> &list, size_t &done, int elapsedMs)
{
	if(done < list.size() && elapsedMs >= list[done].second){
		return &list[done++].first;
	}
	return nullptr;
}

bool DebugAutomation::takeOnce(int at, bool &done, int elapsedMs)
{
	if(at >= 0 && !done && elapsedMs >= at){
		done = true;
		return true;
	}
	return false;
}

bool DebugAutomation::takeMouse(int elapsedMs, Mouse &out)
{
	if(mouse_ && !mouseDone_ && elapsedMs >= mouse_->at){
		mouseDone_ = true;
		out = *mouse_;
		return true;
	}
	return false;
}

void DebugAutomation::sendMouse(ui::UiScript &script, const Mouse &mouse)
{
	script.onMouseMove(mouse.x, mouse.y);
	if(mouse.click){
		script.onMouseButton(1, true, mouse.x, mouse.y);
		script.onMouseButton(1, false, mouse.x, mouse.y);
	}
}

void DebugAutomation::overrideMove(float &forward, float &right) const
{
	if(hasWalk_){
		forward = (walkMode_ == '2' || walkMode_ == '3') ? 0.0f : 1.0f;
		right = walkMode_ == '2' ? 1.0f : walkMode_ == '3' ? -1.0f : right;
	}
}

} // namespace game
