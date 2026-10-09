#if !defined(MODEL_ANIMATIONCLIP_H_)
#define MODEL_ANIMATIONCLIP_H_

#include "model/Skeleton.h"
#include <cstdint>
#include <string>
#include <vector>

namespace model
{

// glTF のアニメーション1つ(クリップ)。ノード(ボーン)の名前ごとに、平行移動・回転・拡大縮小のトラック(キーの時刻と値)を持つ。
// 人型やボーンの名前を前提にしない(ボーンの数・名前は任意。ClipPlayer が名前でスケルトンのボーンに対応づける)。
// 値は、エンジンの座標(PMX と同じ左手系。GltfLoader の GltfAxes と同じ鏡像)に直し、ファイルの休止ポーズ(ノードの translation / rotation / scale)からの
// ずれにしてある(休止ポーズで、回転は単位、移動は0、拡大縮小は1)。向きは「モデルの軸」(休止ポーズの、全部のボーンの回転を戻した向き)で表す。
// これで、ボーンの休止ポーズの回転(Blender が書き出すボーンの向きなど)が、ファイルと再生先のスケルトンで違っても、同じ動きになる
struct AnimationClip
{
	// キーの間の補間(glTF の LINEAR / STEP。CUBICSPLINE は、読み込みで警告して、キーの値だけを LINEAR で使う)
	enum class Interpolation : uint8_t { Linear, Step };
	template<typename T>
	struct Curve
	{
		std::vector<float> times; // 増える順
		std::vector<T> values;
		Interpolation interpolation = Interpolation::Linear;

		bool empty() const { return times.empty(); }
		// 時刻 t の値(最初のキーより前は最初の値、最後のキーより後は最後の値)。回転は球面線形補間
		T sample(float t) const;
	};
	struct Track
	{
		std::string node;
		// 休止ポーズの位置からのずれ(親のボーンの休止ポーズの位置から見た向き = モデルの軸。単位はファイルのまま)
		Curve<Vec3> translation;
		// 休止ポーズからの局所の回転(モデルの軸で表したもの。親のボーンから見た回転で、休止ポーズで単位)
		Curve<Quat> rotation;
		// 休止ポーズに対する拡大縮小の倍率(モデルの軸。1で休止ポーズのまま)
		Curve<Vec3> scale;
	};

	std::string name;
	float duration = 0.0f; // いちばん遅いキーの時刻(秒)
	std::vector<Track> tracks;
};

template<> Vec3 AnimationClip::Curve<Vec3>::sample(float t) const;
template<> Quat AnimationClip::Curve<Quat>::sample(float t) const;

// glTF(.glb / .gltf)の、全部のアニメーションを読む(ファイルの並びのまま。名前の無いアニメーションは "animation<番号>")。
// 読めなければ空(理由は SDL_LogError に出す)
std::vector<AnimationClip> loadAnimationClips(const std::string &fullPath);

} // namespace model

#endif // MODEL_ANIMATIONCLIP_H_
