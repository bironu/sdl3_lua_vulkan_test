#include "scene/game/CreatureDriver.h"
#include <algorithm>
#include <cmath>

namespace game
{

void CreatureDriver::play(State &state, Motion motion) const
{
	if(motion == state.motion){
		return;
	}
	if(motion == state.previous){
		state.fade = 1.0f - state.fade; // 戻る途中なら、そこから戻す
	}
	else{
		state.fade = 0.0f;
	}
	state.previous = state.motion;
	state.motion = motion;
	state.actionTime = 0.0f;
}

void CreatureDriver::advance(State &state, float dt, float speed) const
{
	state.fade = std::min(1.0f, state.fade + dt * blend_);
	float walkStride = stride(state.motion);
	if(walkStride <= 0.0f && state.fade < 1.0f){
		walkStride = stride(state.previous);
	}
	if(walkStride > 0.0f){
		const float phase = state.walkPhase + dt * speed / walkStride;
		state.walkPhase = phase - std::floor(phase);
	}
	state.clock += dt * state.rate;
	state.actionTime += dt * state.rate;
}

bool CreatureDriver::finished(const State &state) const
{
	return state.actionTime >= duration(state.motion);
}

float CreatureDriver::jumpProgress(const State &state) const
{
	if(state.motion != Motion::AttackJump){
		return 0.0f;
	}
	const JumpPhases phases = jumpPhases();
	const float span = phases.airEnd - phases.launchEnd;
	const float t = state.actionTime - phases.launchEnd;
	return span > 1e-4f ? std::clamp(t / span, 0.0f, 1.0f) : (t >= 0.0f ? 1.0f : 0.0f);
}

} // namespace game
