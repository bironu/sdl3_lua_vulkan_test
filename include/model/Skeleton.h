#if !defined(MODEL_SKELETON_H_)
#define MODEL_SKELETON_H_

#include "model/ModelData.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace model
{

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
};

// 回転(単位クォータニオン)。q * v * q^-1 で v を回す(座標系の手系によらず同じ式)
struct Quat
{
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

	static Quat fromAxisAngle(const Vec3 &axis, float radians);
	Quat operator*(const Quat &r) const; // (this * r): rで回してからthisで回す
	Quat conjugate() const { return {-x, -y, -z, w}; }
	Quat normalized() const;
	static Quat slerp(const Quat &a, const Quat &b, float t);
	// ベクトルを回す(q v q^-1)
	Vec3 rotate(const Vec3 &v) const;
};

// 剛体変換 v' = R v + t (Rは3x3回転行列)。ボーンの現在の姿勢(グローバル)やスキニング行列に使う
struct Mat34
{
	float r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	float t[3] = {0, 0, 0};
};

// モデルのボーン階層の、現在の姿勢の計算。
// 使い方: resetPose() → モーションなどからsetBoneRotation/setBoneTranslation → update() → skinMatrices()。
// update()は 付与(append)・順運動学(FK)・IK(CCD法)を、PMXの変形階層の順で解く。
// 座標系はモデルの元の座標系(PMXは左手系)のまま。物理演算(剛体)は扱わない
class Skeleton
{
public:
	explicit Skeleton(const std::vector<ModelBone> &bones);

	size_t boneCount() const { return bones_.size(); }
	// ボーン名(UTF-8)からボーン番号を引く。無ければ-1
	int findBone(const std::string &name) const;
	const ModelBone &bone(int index) const { return bones_[index]; }

	// 全ボーンのアニメーション(回転・移動)・IK・付与を初期(休止ポーズ)へ戻す
	void resetPose();
	// ボーンのアニメーション。回転は親ボーンから見た局所の回転、移動は休止ポーズからのずれ
	void setBoneRotation(int bone, const Quat &rotation);
	void setBoneTranslation(int bone, const Vec3 &translation);
	// いま設定されているボーンのアニメーション(setBoneRotation/setBoneTranslationで入れたもの。resetPose()後は休止ポーズ=回転なし・移動なし)。
	// 2つのモーションの姿勢を混ぜる(クロスフェード)ときに読む
	const Quat &boneRotation(int bone) const { return state_[bone].animRotation; }
	const Vec3 &boneTranslation(int bone) const { return state_[bone].animTranslation; }
	// IKを有効/無効にする(既定は有効)。全体と、IKボーンごと(VMDのIKオン/オフ区間用)
	void setIkEnabled(bool enabled) { ikEnabled_ = enabled; }
	void setIkEnabled(int ikBone, bool enabled);

	void update();

	// スキニング行列: 頂点(休止ポーズの座標)を現在の姿勢の座標へ移す
	const std::vector<Mat34> &skinMatrices() const { return skin_; }
	// ボーンの現在のグローバル位置
	Vec3 globalPosition(int bone) const;
	// ボーンの現在のグローバル姿勢
	const Mat34 &globalTransform(int bone) const { return state_[bone].global; }
	// 休止ポーズでの、ボーンのグローバルな回転(PMXは回転なし=単位)
	const Quat &restGlobalRotation(int bone) const { return restGlobalRotation_[bone]; }
	// 休止ポーズでの、親の座標系で見た親からの位置
	const Vec3 &restLocalTranslation(int bone) const { return restLocalTranslation_[bone]; }
	// 物理演算の結果でボーンのグローバル姿勢を上書きする(update()の後に呼ぶ)。bones[i]の姿勢をglobals[i]にして、
	// 上書きしないボーンは親の変化に合わせて更新し直し、スキニング行列も作り直す
	void applyPhysics(const std::vector<int> &bones, const std::vector<Mat34> &globals);

private:
	struct State
	{
		Quat animRotation;
		Vec3 animTranslation;
		Quat ikRotation;     // IKが加える回転(局所、animRotationの後に掛ける)
		Quat localRotation;  // 付与・IKを含む、最終的な局所の回転
		Vec3 localTranslation;
		Mat34 global;
	};

	void updateBone(int index);
	void updateAllBones();
	void solveIk(int ikBone);
	void computeSkinMatrices();
	void applyIkLimits(const ModelBone::IkLink &link, State &state) const;

	std::vector<ModelBone> bones_;
	std::vector<int> order_;          // 変形階層(layer)→番号の順に並べたボーン番号
	std::vector<State> state_;
	std::vector<Mat34> skin_;
	std::vector<Quat> restGlobalRotation_; // 休止ポーズでの、ボーンのグローバルな回転(PMXは回転なしなので全部単位)
	std::vector<Vec3> restLocalTranslation_; // 休止ポーズでの、親のボーンの座標系で見た、親からの位置
	std::unordered_map<std::string, int> nameToIndex_;
	bool ikEnabled_ = true;
	std::vector<bool> ikBoneEnabled_;
};

} // namespace model

#endif // MODEL_SKELETON_H_
