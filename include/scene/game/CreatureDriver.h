#if !defined(GAME_CREATUREDRIVER_H_)
#define GAME_CREATUREDRIVER_H_

#include "model/Skeleton.h"
#include <cstdint>

namespace game
{

// 敵の動き(姿勢)を作るものの、共通の部分: 動きの種類と、個体ごとの状態(State)の進め方(動きの切り替えと混ぜ具合・歩きの位相・時計)。
// 姿勢の作り方は2通り: ゲームは CreatureClipAnimator(glTF のアニメーションのクリップを再生する)、ツール creature2glb は CreatureAnimator
// (tools/creature/。数式で作り、クリップに焼き込む)。
// 種類ごとに1つ作り、個体ごとの State を動かす
class CreatureDriver
{
public:
	enum class Motion : uint8_t { Idle, WalkFast, WalkSlow, AttackStand, AttackJump, Death };
	struct State
	{
		Motion motion = Motion::Idle;
		Motion previous = Motion::Idle;
		float fade = 1.0f;       // motion の重み(0→1 で、previous から移り終わる)
		float walkPhase = 0.0f;  // 歩きの位相(0〜1で1周期)
		float clock = 0.0f;      // 待機の時計(秒)
		float actionTime = 0.0f; // 攻撃・倒れを始めてからの、動きの時刻(秒。rate を掛けて進む)
		float rate = 1.0f;       // 待機・攻撃の速さの個体差
	};
	// 体全体の大きさの倍率(倒れ込み・着地の潰れと、跳んでいる間の伸び)。描画の側で、地面(モデルの Y=0)を基準に、モデル行列に掛ける
	struct BodyScale
	{
		float vertical = 1.0f;   // 高さ(Y)
		float horizontal = 1.0f; // 前後・左右(X・Z)
	};
	// 攻撃B(ジャンプ)の区間の境(動きの時刻。秒): 溜め(0〜crouchEnd) → 踏み切り(〜launchEnd) → 空中(〜airEnd) → 着地(びたーん)・戻り。
	// 位置(放物線)は EnemyHorde が動かす(空中の区間は、決めた滞空時間に合わせて、時刻を伸縮して進める)
	struct JumpPhases
	{
		float crouchEnd = 0.0f;
		float launchEnd = 0.0f;
		float airEnd = 0.0f;
	};

	virtual ~CreatureDriver() = default;

	// 動きを切り替える(同じなら何もしない)。切り替えの間は、前の動きと混ぜる。攻撃・倒れは、最初から
	void play(State &state, Motion motion) const;
	// 時間を進める。speed は歩く速さ(m/秒。歩きの足の運びの速さに使う)
	void advance(State &state, float dt, float speed) const;
	// 攻撃が終わった(元の姿勢へ戻りきった)か。攻撃でなければ true
	bool finished(const State &state) const;
	// 攻撃B(ジャンプ)の、跳んでいる間の進み具合(跳ぶ前は0、空中の区間で0→1、着地した後は1)
	float jumpProgress(const State &state) const;

	// 攻撃の動きの全体の長さ(秒。rate が1のとき)。攻撃でなければ0
	virtual float duration(Motion motion) const = 0;
	virtual JumpPhases jumpPhases() const = 0;
	// 攻撃A(倒れ込み)で、胴体が前へ倒れている度合い(0〜1。攻撃Aでなければ0)。当たりの箱を前へ伸ばすのに使う
	virtual float fallen(const State &state) const = 0;
	virtual BodyScale bodyScale(const State &state) const = 0;
	// state の姿勢を skeleton に当てる(resetPose から。update は呼び出し側が行う)
	virtual void apply(model::Skeleton &skeleton, const State &state) const = 0;

protected:
	// blend: 動きを切り替えるときに、混ぜて移る速さ(1/秒)
	explicit CreatureDriver(float blend) : blend_(blend) {}
	// 歩きの1周期で進む距離(m。歩きの動きでなければ0)。足の運びの速さは、歩く速さ÷これ
	virtual float stride(Motion motion) const = 0;

private:
	float blend_ = 5.0f;
};

} // namespace game

#endif // GAME_CREATUREDRIVER_H_
