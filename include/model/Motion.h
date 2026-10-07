#if !defined(MODEL_MOTION_H_)
#define MODEL_MOTION_H_

#include "model/MorphSet.h"
#include "model/Skeleton.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace model
{

// ボーンの1つのキーフレーム(VMD)。補間のパラメータは「このキーフレームへ向かう区間」の曲線
struct MotionKey
{
	uint32_t frame = 0; // 30フレーム/秒
	Vec3 translation;   // 休止ポーズからの移動
	Quat rotation;
	// ベジェ曲線の制御点(0〜127を0〜1に直して使う): 移動X・Y・Z、回転の順に、(x1, y1, x2, y2)
	uint8_t interpolation[4][4] = {{20, 20, 107, 107}, {20, 20, 107, 107}, {20, 20, 107, 107}, {20, 20, 107, 107}};
};

struct BoneTrack
{
	std::string name; // UTF-8のボーン名
	std::vector<MotionKey> keys; // フレーム順
};

// モーフの重みのキーフレーム(VMD)。補間曲線は無く、キー間は線形に補間する
struct MorphKey
{
	uint32_t frame = 0;
	float weight = 0.0f;
};

struct MorphTrack
{
	std::string name; // UTF-8のモーフ名
	std::vector<MorphKey> keys; // フレーム順
};

// IKのオン/オフの切り替え(VMDのIKフレーム)
struct IkSwitchKey
{
	uint32_t frame = 0;
	std::vector<std::pair<std::string, bool>> states; // IKボーン名 → オン?
};

// モーション(VMD)。ボーンのキーフレーム列とIKのオン/オフ
struct Motion
{
	std::string modelName;
	std::vector<BoneTrack> tracks;
	std::vector<IkSwitchKey> ikSwitches; // フレーム順
	std::vector<MorphTrack> morphTracks;
	uint32_t lastFrame = 0;              // 最後のキーフレーム
};

// モーションをスケルトンへ当てはめて再生する。モーションのボーン名とスケルトンのボーンを、作成時に対応付ける
// (対応しないボーンのトラックは無視)。FPSは30。使い方: resetPose済みのスケルトンにapply()してから、update()する
class MotionPlayer
{
public:
	// morphsを渡すと、モーフのトラックも対応付ける(モデルにモーフが無ければnullptr)
	MotionPlayer(const std::shared_ptr<const Motion> &motion, const Skeleton &skeleton, const MorphSet *morphs = nullptr);

	// フレーム番号(小数可。キーフレーム間を補間する)の姿勢を、スケルトンのボーンの回転・移動とIKオン/オフに設定する
	void apply(Skeleton &skeleton, float frame) const;
	// フレーム番号の、モーフの重みをmorphsに設定する(モーションに無いモーフの重みは触らない。事前にresetWeights()しておく)
	void applyMorphs(MorphSet &morphs, float frame) const;
	size_t boundMorphTrackCount() const;
	// 対応するモーフがモデルに無かったトラックの名前
	std::vector<std::string> unboundMorphTrackNames() const;
	float lastFrame() const { return static_cast<float>(motion_->lastFrame); }
	// 対応付けできたトラックの数(捩りの別名で単独の捩りボーンに割り当てたものを含む)
	size_t boundTrackCount() const;
	// 対応するボーンがモデルに無かったトラックの名前(モデルとモーションのボーン名の違いの確認用。補助ボーンのトラックは含めない)
	std::vector<std::string> unboundTrackNames() const;

private:
	std::shared_ptr<const Motion> motion_;
	std::vector<int> aliasBone_; // トラックi(別名) → 割り当て先のボーン(無ければ-1)。別名が1つも無ければ空
	std::vector<bool> ignored_;     // トラックi: モデルの形に依存する補助ボーンのため、対応が無くても欠落扱いにしない
	std::vector<int> trackMorph_;  // モーフのトラックi → モーフ番号(無ければ-1)
	std::vector<int> trackBone_;   // トラックi → スケルトンのボーン番号(無ければ-1)
	std::vector<std::vector<int>> ikSwitchBone_; // IKスイッチkのstates[j] → ボーン番号
};

} // namespace model

#endif // MODEL_MOTION_H_
