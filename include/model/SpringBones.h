#if !defined(MODEL_SPRINGBONES_H_)
#define MODEL_SPRINGBONES_H_

#include "model/ModelData.h"
#include "model/Skeleton.h"
#include <memory>
#include <vector>

namespace model
{

// スプリングボーン(VRMの髪やスカートなどの揺れ)。ボーンの鎖ごとに、先端をVerlet積分でバネのように動かし、
// 元の向きへ戻る力・重力・空気抵抗・コライダー(球とカプセル)との衝突を解く。
// 使い方: skeleton.update()でアニメーションを解いた後に step() を呼ぶ。揺れるボーンの姿勢をskeletonへ書き込み、
// スキニング行列も作り直す(Skeleton::applyPhysics)。PMXの剛体(model::Physics)とは同時に使わない
class SpringBones
{
public:
	// スプリングボーンが無ければnullptr
	static std::unique_ptr<SpringBones> create(const ModelData &data, const Skeleton &skeleton);

	// dt秒だけ進める(内部は1/60秒刻み)。dtが0なら、先端の状態は進めずに、現在の姿勢へ合わせ直すだけ
	void step(Skeleton &skeleton, float dt);
	// 先端を現在のボーンの姿勢へ戻して、速度を0にする(モーションの頭出しや大きく動いた直後に呼ぶ)
	void reset(const Skeleton &skeleton);

private:
	SpringBones() = default;
	struct Joint
	{
		int bone = -1;
		int tailBone = -1;      // 次のジョイントのボーン(この先端を動かす)。-1なら仮想の先端(末端のボーン)
		int parentBone = -1;
		float length = 0.0f;
		float localDirection[3] = {0, 1, 0}; // 先端の向き(このボーンの休止ポーズの座標系)
		SpringJoint setting;
		float current[3] = {0, 0, 0}; // 先端の現在の位置(モデル空間)と1つ前の位置
		float previous[3] = {0, 0, 0};
	};
	struct Chain
	{
		std::vector<Joint> joints;
		std::vector<int> colliders; // コライダーの番号(グループを展開した、重複なしのもの)
	};

	void pass(Skeleton &skeleton, float dt, bool commit, std::vector<int> &bones, std::vector<Mat34> &globals);

	std::vector<Chain> chains_;
	std::vector<SpringCollider> colliders_;
	float accumulator_ = 0.0f;
};

} // namespace model

#endif // MODEL_SPRINGBONES_H_
