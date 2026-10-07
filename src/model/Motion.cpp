#include "model/Motion.h"
#include <algorithm>
#include <cmath>

namespace model
{

namespace
{

// ベジェ曲線 (0,0),(x1,y1),(x2,y2),(1,1) で、横軸tのときの縦軸の値(0〜1)を返す。
// 横軸がtになるパラメータuを二分法で求め、そのyを返す
float bezier(const uint8_t *p, float t)
{
	if(t <= 0.0f){ return 0.0f; }
	if(t >= 1.0f){ return 1.0f; }
	const float x1 = p[0] / 127.0f, y1 = p[1] / 127.0f, x2 = p[2] / 127.0f, y2 = p[3] / 127.0f;
	auto curve = [](float a, float b, float u){
		const float v = 1.0f - u;
		return 3.0f * v * v * u * a + 3.0f * v * u * u * b + u * u * u;
	};
	float lo = 0.0f, hi = 1.0f, u = t;
	for(int i = 0; i < 20; ++i){
		u = (lo + hi) * 0.5f;
		if(curve(x1, x2, u) < t){
			lo = u;
		}
		else{
			hi = u;
		}
	}
	return curve(y1, y2, u);
}

} // namespace

MotionPlayer::MotionPlayer(const std::shared_ptr<const Motion> &motion, const Skeleton &skeleton, const MorphSet *morphs)
	: motion_(motion)
{
	trackBone_.reserve(motion_->tracks.size());
	for(const auto &track : motion_->tracks){
		trackBone_.push_back(skeleton.findBone(track.name));
	}
	// 別名: モデルに同名のボーンが無いトラックを、役割が同じ別のボーンへ割り当てる。回転は掛け合わせ、移動は足す
	//  - 捩りボーン: 「左腕捩1」「右手捩3」のような番号付きのトラックを、番号を除いた単独のボーン(左腕捩)へ
	//    (番号付きの捩りボーンを持つモデル向けのモーションを、単独の捩りボーンを持つモデルで再生するため)
	//  - 「足IK親」「操作中心」: IKボーンや全体の親に当たるボーンの、モデルごとの呼び名の違い
	// さらに、モデルの形に依存する補助ボーン(足D・ダミー・腰キャンセルなど)は、対応するものが無くても欠落扱いにしない(ignored_)
	bool anyAlias = false;
	std::vector<int> alias(motion_->tracks.size(), -1);
	ignored_.assign(motion_->tracks.size(), false);
	static const std::pair<const char *, const char *> kAliases[] = {
		{"左足IK親", "左足ＩＫ"}, {"右足IK親", "右足ＩＫ"}, {"左足ＩＫ親", "左足ＩＫ"}, {"右足ＩＫ親", "右足ＩＫ"},
		{"操作中心", "全ての親"},
	};
	static const char *const kIgnorableMarks[] = {"ダミー", "接続基部", "足先EX", "腰キャンセル", "ひざD", "足首D", "足D"};
	for(size_t i = 0; i < motion_->tracks.size(); ++i){
		const std::string &name = motion_->tracks[i].name;
		if(trackBone_[i] >= 0){
			continue;
		}
		for(const auto &entry : kAliases){
			if(name == entry.first){
				alias[i] = skeleton.findBone(entry.second);
			}
		}
		if(alias[i] < 0 && name.find("捩") != std::string::npos){
			// 末尾の数字(半角または全角)を取り除く
			std::string base = name;
			while(!base.empty() && base.back() >= '0' && base.back() <= '9'){
				base.pop_back();
			}
			static const char *const fullWidthDigits[] = {"０", "１", "２", "３", "４", "５", "６", "７", "８", "９"};
			for(bool stripped = true; stripped;){
				stripped = false;
				for(const char *digit : fullWidthDigits){
					const std::string d(digit);
					if(base.size() >= d.size() && base.compare(base.size() - d.size(), d.size(), d) == 0){
						base.resize(base.size() - d.size());
						stripped = true;
					}
				}
			}
			if(base != name){
				alias[i] = skeleton.findBone(base);
			}
		}
		anyAlias = anyAlias || alias[i] >= 0;
		if(alias[i] < 0){
			for(const char *mark : kIgnorableMarks){
				if(name.find(mark) != std::string::npos){
					ignored_[i] = true;
				}
			}
		}
	}
	if(anyAlias){
		aliasBone_ = std::move(alias);
	}
	for(const auto &track : motion_->morphTracks){
		trackMorph_.push_back(morphs ? morphs->findMorph(track.name) : -1);
	}
	for(const auto &key : motion_->ikSwitches){
		std::vector<int> bones;
		for(const auto &state : key.states){
			bones.push_back(skeleton.findBone(state.first));
		}
		ikSwitchBone_.push_back(std::move(bones));
	}
}

size_t MotionPlayer::boundTrackCount() const
{
	size_t count = 0;
	for(size_t i = 0; i < trackBone_.size(); ++i){
		count += (trackBone_[i] >= 0 || (!aliasBone_.empty() && aliasBone_[i] >= 0)) ? 1 : 0;
	}
	return count;
}

std::vector<std::string> MotionPlayer::unboundTrackNames() const
{
	std::vector<std::string> names;
	for(size_t i = 0; i < trackBone_.size(); ++i){
		if(trackBone_[i] < 0 && (aliasBone_.empty() || aliasBone_[i] < 0) && !ignored_[i]){
			names.push_back(motion_->tracks[i].name);
		}
	}
	return names;
}

size_t MotionPlayer::boundMorphTrackCount() const
{
	return static_cast<size_t>(std::count_if(trackMorph_.begin(), trackMorph_.end(), [](int m){ return m >= 0; }));
}

std::vector<std::string> MotionPlayer::unboundMorphTrackNames() const
{
	std::vector<std::string> names;
	for(size_t i = 0; i < trackMorph_.size(); ++i){
		if(trackMorph_[i] < 0){
			names.push_back(motion_->morphTracks[i].name);
		}
	}
	return names;
}

void MotionPlayer::applyMorphs(MorphSet &morphs, float frame) const
{
	for(size_t i = 0; i < motion_->morphTracks.size(); ++i){
		const int morph = trackMorph_[i];
		const auto &keys = motion_->morphTracks[i].keys;
		if(morph < 0 || keys.empty()){
			continue;
		}
		const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
			[](float f, const MorphKey &k){ return f < static_cast<float>(k.frame); });
		float weight;
		if(next == keys.begin()){
			weight = keys.front().weight;
		}
		else if(next == keys.end()){
			weight = keys.back().weight;
		}
		else{
			const MorphKey &k0 = *(next - 1);
			const float span = static_cast<float>(next->frame - k0.frame);
			const float t = span > 0.0f ? (frame - static_cast<float>(k0.frame)) / span : 0.0f;
			weight = k0.weight + (next->weight - k0.weight) * t;
		}
		morphs.setWeight(morph, weight);
	}
}

// 1つのトラックの、フレームframeでの姿勢(キーフレーム間はベジェ曲線で補間)
static void evaluateTrack(const std::vector<MotionKey> &keys, float frame, Vec3 &translation, Quat &rotation)
{
	// frame以下で最後のキーと、その次のキーを探す
	const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
		[](float f, const MotionKey &k){ return f < static_cast<float>(k.frame); });
	if(next == keys.begin()){ // 最初のキーより前: 最初のキーの姿勢
		translation = keys.front().translation;
		rotation = keys.front().rotation;
		return;
	}
	const MotionKey &k0 = *(next - 1);
	if(next == keys.end()){ // 最後のキーより後: 最後のキーの姿勢
		translation = k0.translation;
		rotation = k0.rotation;
		return;
	}
	const MotionKey &k1 = *next;
	const float span = static_cast<float>(k1.frame - k0.frame);
	const float t = span > 0.0f ? (frame - static_cast<float>(k0.frame)) / span : 0.0f;
	// 補間曲線は、向かう先のキー(k1)に入っている
	const float sx = bezier(k1.interpolation[0], t);
	const float sy = bezier(k1.interpolation[1], t);
	const float sz = bezier(k1.interpolation[2], t);
	const float sr = bezier(k1.interpolation[3], t);
	translation = {k0.translation.x + (k1.translation.x - k0.translation.x) * sx,
		k0.translation.y + (k1.translation.y - k0.translation.y) * sy,
		k0.translation.z + (k1.translation.z - k0.translation.z) * sz};
	rotation = Quat::slerp(k0.rotation, k1.rotation, sr);
}

void MotionPlayer::apply(Skeleton &skeleton, float frame) const
{
	// 別名のトラック(モデルに無い「左腕捩1」「左足IK親」など)は、割り当て先のボーンへ回転を掛け合わせ、移動を足す。
	// 捩りは同じ軸まわりの回転なので、掛ける順序は結果に影響しない(IK親などは、親の回転が移動に及ぼす影響を無視した近似)
	std::vector<Quat> twist;
	std::vector<Vec3> twistTranslation;
	std::vector<bool> hasTwist;
	if(!aliasBone_.empty()){
		twist.assign(skeleton.boneCount(), Quat());
		twistTranslation.assign(skeleton.boneCount(), Vec3());
		hasTwist.assign(skeleton.boneCount(), false);
		for(size_t i = 0; i < motion_->tracks.size(); ++i){
			const int bone = aliasBone_[i];
			if(bone < 0 || motion_->tracks[i].keys.empty()){
				continue;
			}
			Vec3 t;
			Quat q;
			evaluateTrack(motion_->tracks[i].keys, frame, t, q);
			twist[bone] = twist[bone] * q;
			twistTranslation[bone] = {twistTranslation[bone].x + t.x, twistTranslation[bone].y + t.y, twistTranslation[bone].z + t.z};
			hasTwist[bone] = true;
		}
	}

	for(size_t i = 0; i < motion_->tracks.size(); ++i){
		const int bone = trackBone_[i];
		const auto &keys = motion_->tracks[i].keys;
		if(bone < 0 || keys.empty()){
			continue;
		}
		Vec3 translation;
		Quat rotation;
		evaluateTrack(keys, frame, translation, rotation);
		if(!hasTwist.empty() && hasTwist[bone]){
			rotation = rotation * twist[bone];
			translation = {translation.x + twistTranslation[bone].x, translation.y + twistTranslation[bone].y, translation.z + twistTranslation[bone].z};
			hasTwist[bone] = false; // 掛け合わせ済み
		}
		skeleton.setBoneTranslation(bone, translation);
		skeleton.setBoneRotation(bone, rotation);
	}
	// 本体のトラックが無いボーンは、別名トラックの回転と移動だけを設定する
	for(size_t bone = 0; bone < hasTwist.size(); ++bone){
		if(hasTwist[bone]){
			skeleton.setBoneTranslation(static_cast<int>(bone), twistTranslation[bone]);
			skeleton.setBoneRotation(static_cast<int>(bone), twist[bone]);
		}
	}

	// IKのオン/オフ: frame以下で最後のスイッチの状態を適用する(まだ無ければ全部オンのまま)
	const auto &switches = motion_->ikSwitches;
	const auto it = std::upper_bound(switches.begin(), switches.end(), frame,
		[](float f, const IkSwitchKey &k){ return f < static_cast<float>(k.frame); });
	if(it != switches.begin()){
		const size_t index = static_cast<size_t>((it - 1) - switches.begin());
		const auto &key = switches[index];
		for(size_t j = 0; j < key.states.size(); ++j){
			skeleton.setIkEnabled(ikSwitchBone_[index][j], key.states[j].second);
		}
	}
}

} // namespace model
