#include "scene/game/CreatureClipAnimator.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace game
{

namespace
{
// 時刻 t の、start〜end の区間の進み具合(0〜1)
float ramp(float t, float start, float end)
{
	return end > start ? std::clamp((t - start) / (end - start), 0.0f, 1.0f) : (t >= start ? 1.0f : 0.0f);
}
}

CreatureClipAnimator::CreatureClipAnimator(std::shared_ptr<const std::vector<model::AnimationClip>> clips, const model::Skeleton &skeleton,
	const CreatureClipMotion &motion, float walkFastSpeed, float walkSlowSpeed, float blend, float metersToModel)
	: CreatureDriver(blend), player_(std::move(clips), skeleton, metersToModel, motion.rootMotionBones), stand_(motion.stand), jump_(motion.jump)
{
	const CreatureClipMotion::Entry *entries[] = {&motion.idle, &motion.walkFast, &motion.walkSlow, &motion.attackStand, &motion.attackJump, &motion.death};
	for(size_t i = 0; i < slots_.size(); ++i){
		const CreatureClipMotion::Entry &entry = *entries[i];
		Slot &slot = slots_[i];
		slot.clip = player_.find(entry.clip);
		slot.loop = entry.loop;
		if(slot.clip < 0){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "creature clips: no clip '%s' in %s (that motion keeps the rest pose)", entry.clip.c_str(), motion.path.c_str());
		}
	}
	// 歩きの1周で進む距離: 表に無ければ、歩きの速さ × クリップの長さ(その速さで歩くと、クリップの速さのまま再生される)
	const float speeds[2] = {walkFastSpeed, walkSlowSpeed};
	for(size_t k = 0; k < 2; ++k){
		Slot &slot = slots_[static_cast<size_t>(Motion::WalkFast) + k];
		const float given = entries[1 + k]->stride;
		slot.stride = std::max(given > 0.0f ? given : speeds[k] * player_.duration(slot.clip), 0.05f);
	}
}

float CreatureClipAnimator::stride(Motion motion) const
{
	return motion == Motion::WalkFast || motion == Motion::WalkSlow ? slots_[static_cast<size_t>(motion)].stride : 0.0f;
}

float CreatureClipAnimator::duration(Motion motion) const
{
	return motion == Motion::AttackStand || motion == Motion::AttackJump ? player_.duration(slots_[static_cast<size_t>(motion)].clip) : 0.0f;
}

CreatureDriver::JumpPhases CreatureClipAnimator::jumpPhases() const
{
	return jump_;
}

float CreatureClipAnimator::fallen(const State &state) const
{
	if(state.motion != Motion::AttackStand){
		return 0.0f;
	}
	const float t = state.actionTime;
	return ramp(t, stand_.fallStart, stand_.fallEnd) * (1.0f - ramp(t, stand_.recoverStart, stand_.recoverEnd));
}

model::ClipPlayer::Layer CreatureClipAnimator::layer(Motion motion, const State &state) const
{
	const Slot &slot = slots_[static_cast<size_t>(motion)];
	model::ClipPlayer::Layer result;
	result.clip = slot.clip;
	result.loop = slot.loop;
	switch(motion){
	case Motion::Idle: result.time = state.clock; break;
	case Motion::WalkFast: case Motion::WalkSlow: result.time = state.walkPhase * player_.duration(slot.clip); break;
	default: result.time = state.actionTime; break;
	}
	return result;
}

float CreatureClipAnimator::weight(const State &state)
{
	const float t = std::clamp(state.fade, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// 根のボーンの拡大縮小(モデルの軸): 高さは Y、前後・左右は X・Z の平均
CreatureDriver::BodyScale CreatureClipAnimator::bodyScale(const State &state) const
{
	const model::Vec3 s = player_.rootScale(layer(state.previous, state), layer(state.motion, state), weight(state));
	return {s.y, 0.5f * (s.x + s.z)};
}

void CreatureClipAnimator::apply(model::Skeleton &skeleton, const State &state) const
{
	player_.apply(skeleton, layer(state.previous, state), layer(state.motion, state), weight(state));
}

} // namespace game
