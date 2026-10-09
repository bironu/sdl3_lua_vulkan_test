#include "model/ClipPlayer.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace model
{

ClipPlayer::ClipPlayer(std::shared_ptr<const std::vector<AnimationClip>> clips, const Skeleton &skeleton, float unitScale,
	const std::vector<std::string> &rootMotionBones)
	: clips_(std::move(clips)), unitScale_(unitScale)
{
	const size_t boneCount = skeleton.boneCount();
	bones_.resize(boneCount);
	int root = -1; // 拡大縮小を返す、根のボーン
	for(size_t b = 0; b < boneCount; ++b){
		const int bone = static_cast<int>(b);
		const int parent = skeleton.bone(bone).parent;
		Bone &info = bones_[b];
		info.fromModel = skeleton.restGlobalRotation(bone).conjugate();
		info.fromModelParent = parent >= 0 ? skeleton.restGlobalRotation(parent).conjugate() : Quat{};
		info.rootMotion = rootMotionBones.empty() ? parent < 0
			: std::find(rootMotionBones.begin(), rootMotionBones.end(), skeleton.bone(bone).name) != rootMotionBones.end();
		if(root < 0 && parent < 0){
			root = bone;
		}
	}
	if(!clips_){
		return;
	}
	for(const AnimationClip &clip : *clips_){
		Clip &binding = bindings_.emplace_back();
		binding.trackOfBone.assign(boneCount, -1);
		for(size_t t = 0; t < clip.tracks.size(); ++t){
			const AnimationClip::Track &track = clip.tracks[t];
			const int bone = skeleton.findBone(track.node);
			if(bone < 0){
				continue; // スケルトンに無いノード(メッシュのノードなど)
			}
			binding.trackOfBone[static_cast<size_t>(bone)] = static_cast<int>(t);
			if(track.scale.empty()){
				continue;
			}
			if(bone == root){
				binding.scaleTrack = static_cast<int>(t);
			}
			else{
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "clip '%s': the scale of '%s' is ignored (only the root bone's scale is used)", clip.name.c_str(),
					track.node.c_str());
			}
		}
	}
}

int ClipPlayer::find(const std::string &name) const
{
	if(!clips_){
		return -1;
	}
	const auto it = std::find_if(clips_->begin(), clips_->end(), [&](const AnimationClip &clip){ return clip.name == name; });
	return it == clips_->end() ? -1 : static_cast<int>(it - clips_->begin());
}

const AnimationClip *ClipPlayer::clip(int index) const
{
	return clips_ && index >= 0 && static_cast<size_t>(index) < clips_->size() ? &(*clips_)[static_cast<size_t>(index)] : nullptr;
}

float ClipPlayer::duration(int index) const
{
	const AnimationClip *c = clip(index);
	return c ? c->duration : 0.0f;
}

float ClipPlayer::sampleTime(const Layer &layer) const
{
	const float length = duration(layer.clip);
	if(length <= 0.0f){
		return 0.0f;
	}
	if(layer.loop){
		const float t = std::fmod(layer.time, length);
		return t < 0.0f ? t + length : t;
	}
	return std::clamp(layer.time, 0.0f, length);
}

// クリップの値はモデルの軸なので、ボーンの休止ポーズの軸へ直す: 回転は R^-1 E R(R = 休止ポーズのグローバルな回転)、
// 移動は、親の休止ポーズの軸へ回して、単位を合わせる
void ClipPlayer::sample(const Layer &layer, size_t bone, Quat &rotation, Vec3 &translation) const
{
	rotation = {};
	translation = {};
	const AnimationClip *c = clip(layer.clip);
	const int t = c ? bindings_[static_cast<size_t>(layer.clip)].trackOfBone[bone] : -1;
	if(t < 0){
		return;
	}
	const AnimationClip::Track &track = c->tracks[static_cast<size_t>(t)];
	const Bone &info = bones_[bone];
	const float time = sampleTime(layer);
	if(!track.rotation.empty()){
		rotation = info.fromModel * track.rotation.sample(time) * info.fromModel.conjugate();
	}
	if(!track.translation.empty() && !info.rootMotion){
		const Vec3 d = track.translation.sample(time);
		translation = info.fromModelParent.rotate({d.x * unitScale_, d.y * unitScale_, d.z * unitScale_});
	}
}

void ClipPlayer::apply(Skeleton &skeleton, const Layer &from, const Layer &to, float weight) const
{
	skeleton.resetPose();
	const bool hasFrom = clip(from.clip) && weight < 1.0f, hasTo = clip(to.clip) && weight > 0.0f;
	if(!hasFrom && !hasTo){
		return;
	}
	const std::vector<int> *fromTracks = hasFrom ? &bindings_[static_cast<size_t>(from.clip)].trackOfBone : nullptr;
	const std::vector<int> *toTracks = hasTo ? &bindings_[static_cast<size_t>(to.clip)].trackOfBone : nullptr;
	const float w = hasFrom && hasTo ? weight : (hasTo ? 1.0f : 0.0f);
	for(size_t b = 0; b < bones_.size(); ++b){
		const bool a = fromTracks && (*fromTracks)[b] >= 0, c = toTracks && (*toTracks)[b] >= 0;
		if(!a && !c){
			continue;
		}
		Quat r0, r1;
		Vec3 t0, t1;
		if(a){
			sample(from, b, r0, t0);
		}
		if(c){
			sample(to, b, r1, t1);
		}
		const int bone = static_cast<int>(b);
		skeleton.setBoneRotation(bone, w <= 0.0f ? r0 : (w >= 1.0f ? r1 : Quat::slerp(r0, r1, w)));
		skeleton.setBoneTranslation(bone, {t0.x + (t1.x - t0.x) * w, t0.y + (t1.y - t0.y) * w, t0.z + (t1.z - t0.z) * w});
	}
}

Vec3 ClipPlayer::rootScale(const Layer &from, const Layer &to, float weight) const
{
	auto scaleOf = [&](const Layer &layer){
		const AnimationClip *c = clip(layer.clip);
		const int t = c ? bindings_[static_cast<size_t>(layer.clip)].scaleTrack : -1;
		return t >= 0 ? c->tracks[static_cast<size_t>(t)].scale.sample(sampleTime(layer)) : Vec3{1.0f, 1.0f, 1.0f};
	};
	const float w = clip(from.clip) ? (clip(to.clip) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f) : 1.0f;
	const Vec3 a = w < 1.0f ? scaleOf(from) : Vec3{}, b = w > 0.0f ? scaleOf(to) : Vec3{};
	return {a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w};
}

} // namespace model
