#if !defined(MODEL_RETARGET_H_)
#define MODEL_RETARGET_H_

#include "model/Skeleton.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace model
{

// MMDのモデル(PMX)にモーションを当てた姿勢を、VRMのモデルへ写す(リターゲット)。
// MMDのボーン名とVRMのヒューマノイドのボーン名を対応づけ、ボーンごとに「ワールド空間での向き」を合わせる:
//   VRMのボーンの向き = (MMD側の、休止ポーズからの回転) * (休止ポーズの向きの差を埋める回転) * (VRMの休止ポーズの向き)
// 休止ポーズの向きの差(MMDはAポーズ、VRMはTポーズなど)は、そのボーンから次のボーンへの方向の差から求める。
// MMD側の足IK・付与(捩りなど)は、MMD側のスケルトンですでに解けているので、結果の向きだけを使う(VRM側にIKは要らない)。
// 腰の移動(センター/グルーブなど)は、足の付け根の高さの比で拡大縮小して、VRMの腰へ写す。
// リターゲットの結果(中間表現): VRMの各ボーンの「ワールド空間での、休止ポーズからの回転」と、腰の移動。
// MMDのモデルから求める(Retargeter::computePose)ほか、VRMA(Vrma.h)からも作れる。VRMのスケルトンへの当てはめ(applyPose)は共通
struct RetargetPose
{
	std::vector<Quat> worldDelta; // targetのボーン番号ごと。対応づけのないボーンは単位
	std::vector<bool> mapped;
	Vec3 hipsTranslation;         // 腰の移動(モデル空間、ワールドの向き)
	int hipsBone = -1;
};

class Retargeter
{
public:
	// source: MMDのスケルトン(ボーン名はMMDのもの)。target: VRMのスケルトン。humanoid: VRMのヒューマノイドの名前→ボーン番号。
	// 腰など、土台となるボーンが対応づけできなければnullptr
	static std::unique_ptr<Retargeter> create(const Skeleton &source, const Skeleton &target,
		const std::vector<std::pair<std::string, int>> &humanoid);

	// sourceの現在の姿勢(update()済み)を、targetへ写す(targetのresetPose()は不要。全ボーンを設定し直す)
	void apply(const Skeleton &source, Skeleton &target) const;
	// 同、2段に分けたもの: 中間表現を求める → targetへ当てはめる
	void computePose(const Skeleton &source, const Skeleton &target, RetargetPose &out) const;
	static void applyPose(const RetargetPose &pose, Skeleton &target);
	// targetのボーンのVRMのヒューマノイド名(対応づけが無ければ空文字列)
	const std::string &humanoidName(int targetBone) const { return targetHumanoid_[static_cast<size_t>(targetBone)]; }
	size_t mappedCount() const { return pairs_.size(); }

private:
	struct Pair
	{
		int source = -1;
		int target = -1;
		float correction[3][3]; // 休止ポーズの向きの差を埋める回転(ワールド空間)
	};
	Retargeter() = default;

	std::vector<Pair> pairs_;
	std::vector<std::string> targetHumanoid_;
	std::vector<int> targetToPair_; // targetのボーン番号 → pairs_の添字(無ければ-1)
	int hipsPair_ = -1;
	float translationScale_ = 1.0f; // MMDの移動量(MMD単位) → VRMの移動量(メートル)
	float hipsRest_[3] = {0, 0, 0};
};

} // namespace model

#endif // MODEL_RETARGET_H_
