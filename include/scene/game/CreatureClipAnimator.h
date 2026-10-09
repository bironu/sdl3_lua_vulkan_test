#if !defined(GAME_CREATURECLIPANIMATOR_H_)
#define GAME_CREATURECLIPANIMATOR_H_

#include "model/ClipPlayer.h"
#include "scene/game/CreatureDriver.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace game
{

// クリップで動かすときの、動きの名前 → クリップの表(res/lua/data/enemies.lua の clips と motions)。時間は秒(クリップの時刻)
struct CreatureClipMotion
{
	std::string path; // クリップを読む glTF(.glb / .gltf。リポジトリ直下からの相対パス。既定は、モデルと同じファイル)。空なら、クリップでは動かさない(PMXの歩き)
	struct Entry
	{
		std::string clip;    // クリップの名前(glTF のアニメーションの名前)
		bool loop = false;   // 繰り返す(待機・歩き)
		float stride = 0.0f; // 歩き: クリップの1周で進む距離(m)。0なら、歩きの速さ(walkFast / walkSlow の speed)× クリップの長さ
	};
	Entry idle{"idle", true}, walkFast{"walkFast", true}, walkSlow{"walkSlow", true}, attackStand{"attackStand"}, attackJump{"attackJump"}, death{"death"};
	// 攻撃A の区間の境: 倒れ込み(fallStart〜fallEnd)と、起き上がり(recoverStart〜recoverEnd)。当たりの箱を前へ伸ばす度合いに使う
	struct Stand
	{
		float fallStart = 1.5f, fallEnd = 1.9f, recoverStart = 2.5f, recoverEnd = 3.2f;
	} stand;
	// 攻撃B の区間の境(溜めの終わり・踏み切りの終わり・空中の終わり)。空中の区間は、敵が決めた滞空時間に合わせて伸縮して再生する
	CreatureDriver::JumpPhases jump{0.45f, 0.55f, 1.15f};
	std::vector<std::string> rootMotionBones; // 平行移動を当てない(ルートモーションの)ボーン。空なら、親の無いボーン(root)
};

// 敵の動きを、glTF のアニメーションのクリップの再生で作る。ボーンの数・名前は任意(ClipPlayer がクリップのトラックとボーンを名前で対応づける。特定の生き物の構造を前提にしない)。
// 動き(待機・歩き・攻撃)ごとに、表のクリップを再生し、切り替えの間は、前のクリップとクロスフェードする。
// 時刻: 待機は個体の時計、歩きは歩きの位相 × クリップの長さ、攻撃・倒れは始めてからの時刻(攻撃B の空中の区間は、EnemyHorde が伸縮して進める)。
// クリップの根のボーンの拡大縮小(潰れ・伸び)は bodyScale で返す。根のボーンの移動(ルートモーション)は当てない(前進・高さは EnemyHorde が決める)
class CreatureClipAnimator : public CreatureDriver
{
public:
	// clips を skeleton に当てる。walkSpeeds は、歩きの1周で進む距離を、クリップの長さから決めるときの、早歩き・ゆっくり歩きの速さ(m/秒)。
	// metersToModel は、メートル(クリップの単位)→モデルの単位
	CreatureClipAnimator(std::shared_ptr<const std::vector<model::AnimationClip>> clips, const model::Skeleton &skeleton, const CreatureClipMotion &motion,
		float walkFastSpeed, float walkSlowSpeed, float blend, float metersToModel);

	float duration(Motion motion) const override;
	JumpPhases jumpPhases() const override;
	float fallen(const State &state) const override;
	BodyScale bodyScale(const State &state) const override;
	void apply(model::Skeleton &skeleton, const State &state) const override;

protected:
	float stride(Motion motion) const override;

private:
	struct Slot
	{
		int clip = -1;
		bool loop = false;
		float stride = 0.0f;
	};
	model::ClipPlayer::Layer layer(Motion motion, const State &state) const;
	// クロスフェードの、今の動きの重み(fade を滑らかにしたもの)
	static float weight(const State &state);

	model::ClipPlayer player_;
	std::array<Slot, 6> slots_; // Motion の順
	CreatureClipMotion::Stand stand_;
	JumpPhases jump_;
};

} // namespace game

#endif // GAME_CREATURECLIPANIMATOR_H_
