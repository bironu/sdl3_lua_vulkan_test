#if !defined(MODEL_VRMA_H_)
#define MODEL_VRMA_H_

#include "model/ModelData.h"
#include "model/Motion.h"
#include "model/MorphSet.h"
#include "model/Retarget.h"
#include "model/Skeleton.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace model
{

// VRMA(VRM Animation。VRMC_vrm_animationを持つglTF 2.0のバイナリ .vrma)の中身。モデルを選ばず、どのVRMにも当てられる。
// 回転は「正規化された」ヒューマノイドの局所回転(休止ポーズを全部回転なし=Tポーズ・正面+Zとみなした座標。親は、ヒューマノイドの親ボーン)で、
// 座標系はglTF(右手系、正面+Z)。腰だけ移動も持つ
struct HumanoidAnimation
{
	struct RotationTrack
	{
		std::string bone; // VRMのヒューマノイドのボーン名(hips, spine, leftUpperArm など)
		std::vector<float> times;
		std::vector<Quat> values;
	};
	struct ScalarTrack
	{
		std::string name; // VRMの表情の名前(blink, aa, happy など)
		std::vector<float> times;
		std::vector<float> values;
	};
	struct RestNode
	{
		std::string bone;
		std::string parent; // ヒューマノイドの親(無ければ空)
		Vec3 translation;   // 親から見た休止ポーズの位置(glTFの座標。単位はメートル)
	};

	std::vector<RotationTrack> rotations;
	std::vector<float> hipsTimes;
	std::vector<Vec3> hipsTranslations; // 腰のノードの位置(glTFの座標)
	Vec3 hipsRest;                      // 腰の休止ポーズでの位置
	std::vector<ScalarTrack> expressions;
	std::vector<RestNode> rest;         // 保存時に、ヒューマノイドの階層を書くための情報
	float duration = 0.0f;
};

// VRMAを読む/書く。失敗したらfalse/nullptr(理由はSDL_LogErrorに出す)
std::shared_ptr<HumanoidAnimation> loadVrma(const std::string &fullPath);
bool saveVrma(const std::string &fullPath, const HumanoidAnimation &animation);

// MMDのモデル(PMX)とモーション(VMD)を、VRMのモデルに合わせてリターゲットして、VRMAにする(フレームごとに焼き込む)。
// 変換にはMMDのモデル(ボーン階層・IK・付与の計算)が要るが、できたVRMAは、MMDのモデルが無くても再生できる。
// fpsは焼き込みの秒間フレーム数(VMDと同じ30が自然)。失敗したらfalse
bool convertVmdToVrma(const ModelData &mmdModel, const Motion &motion, const ModelData &vrmModel, const std::string &outPath, float fps = 30.0f);

// VRMAをVRMのスケルトンへ当てはめて再生する(MMDのモデルは要らない)。
// 腰の移動は、モデルの腰の高さの比で拡大縮小する。表情(blink/aaなど)は、モデルの表情(MorphSet)へ書く
class VrmaPlayer
{
public:
	// vrmModel: 再生先のVRMのデータ(ヒューマノイドの対応・座標系の符号を使う)。ヒューマノイドが対応づけできなければnullptr
	static std::unique_ptr<VrmaPlayer> create(const std::shared_ptr<const HumanoidAnimation> &animation, const ModelData &vrmModel,
		const Skeleton &target, const MorphSet *morphs);

	// 時刻(秒)の姿勢を、targetのボーンへ設定する(morphsがnullptrでなければ、表情の重みも。事前にresetWeights()しておく)
	void apply(Skeleton &target, MorphSet *morphs, float seconds) const;
	float duration() const { return animation_->duration; }
	// trueにすると、腰の水平方向(glTFのx, z)の移動を無視して、その場で足踏みする(歩きのモーションを、キャラの移動に合わせて使うとき用。
	// 腰の高さ(y)の上下は残す)。モーション自体が前へ進む(ルートモーション)と、ループで元の位置へ戻って見えるため
	void setInPlace(bool inPlace) { inPlace_ = inPlace; }

private:
	VrmaPlayer() = default;
	bool inPlace_ = false;
	std::shared_ptr<const HumanoidAnimation> animation_;
	std::vector<int> trackBone_;      // トラックi → targetのボーン番号
	std::vector<int> expressionMorph_; // 表情トラックi → targetのモーフ番号(無ければ-1)
	float axisSign_[3] = {1.0f, 1.0f, 1.0f};
	int hipsBone_ = -1;
	float hipsScale_ = 1.0f;
};

} // namespace model

#endif // MODEL_VRMA_H_
