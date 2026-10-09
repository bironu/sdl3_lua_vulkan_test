#if !defined(MODEL_CLIPPLAYER_H_)
#define MODEL_CLIPPLAYER_H_

#include "model/AnimationClip.h"
#include "model/Skeleton.h"
#include <memory>
#include <string>
#include <vector>

namespace model
{

// AnimationClip を、スケルトンのボーンへ当てて再生する。ボーンの名前 → スケルトンのボーン番号の対応を、作るときに1回だけ求める
// (同じ形のスケルトン(同じモデルデータ)なら、1つを何体にでも使える。再生の状態は持たない: 時刻は呼び出し側が決めて渡す)。
// 人型や特定のボーンの名前を前提にしない(名前が一致したボーンだけを動かし、合わないトラックは無視する)。
// - 2つのクリップのクロスフェード(apply の weight)、ループの有無(Layer::loop)。再生の速さは、呼び出し側が時刻の進め方で決める
// - ルートモーション: クリップは「その場」の姿勢だけを持つ決まり。ルートモーションのボーン(既定は、親の無いボーン)の平行移動は、当てない
//   (前進・高さは、呼び出し側(ゲーム)が決める)。回転と、他のボーン(胴体など)の移動は、そのまま当てる
// - 拡大縮小: スケルトンは回転と移動しか持てないので、根のボーン(親の無い最初のボーン)の拡大縮小だけを rootScale で返す(描画のモデル行列に掛ける)
class ClipPlayer
{
public:
	// unitScale: クリップの長さの単位 → スケルトンの単位の倍率。rootMotionBones: 平行移動を当てないボーンの名前(空なら、親の無いボーン)
	ClipPlayer(std::shared_ptr<const std::vector<AnimationClip>> clips, const Skeleton &skeleton, float unitScale,
		const std::vector<std::string> &rootMotionBones = {});

	// 名前のクリップの番号(無ければ -1)
	int find(const std::string &name) const;
	// クリップの長さ(秒。番号が無効なら0)
	float duration(int clip) const;

	// 再生するクリップと時刻。loop なら時刻をクリップの長さで割った余りに、でなければ 0〜長さに収める
	struct Layer
	{
		int clip = -1;
		float time = 0.0f;
		bool loop = false;
	};
	// from と to を、to の重み weight(0〜1)で混ぜた姿勢を、skeleton に当てる(resetPose から。update は呼び出し側)。
	// 片方のクリップが無効なら、もう片方だけ。どちらかにしかトラックの無いボーンは、もう片方を休止ポーズとして混ぜる
	void apply(Skeleton &skeleton, const Layer &from, const Layer &to, float weight) const;
	// 根のボーンの拡大縮小(モデルの軸。from と to を weight で混ぜる。トラックが無ければ1)
	Vec3 rootScale(const Layer &from, const Layer &to, float weight) const;

private:
	struct Bone
	{
		Quat fromModel;       // モデルの軸 → このボーンの休止ポーズの局所の軸(休止ポーズのグローバルな回転の逆)
		Quat fromModelParent; // 同、親のボーンの局所の軸
		bool rootMotion = false;
	};
	struct Clip
	{
		std::vector<int> trackOfBone; // ボーンの番号 → トラックの番号(無ければ -1)
		int scaleTrack = -1;          // 根のボーンの拡大縮小のトラック
	};
	const AnimationClip *clip(int index) const;
	float sampleTime(const Layer &layer) const;
	// layer の、ボーン b の局所の回転・移動(スケルトンのボーンの軸・単位)。トラックが無ければ休止ポーズ
	void sample(const Layer &layer, size_t bone, Quat &rotation, Vec3 &translation) const;

	std::shared_ptr<const std::vector<AnimationClip>> clips_;
	std::vector<Bone> bones_;
	std::vector<Clip> bindings_;
	float unitScale_ = 1.0f;
};

} // namespace model

#endif // MODEL_CLIPPLAYER_H_
