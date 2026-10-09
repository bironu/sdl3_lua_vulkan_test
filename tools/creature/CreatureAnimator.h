#if !defined(CREATURE_CREATUREANIMATOR_H_)
#define CREATURE_CREATUREANIMATOR_H_

#include "scene/game/CreatureDriver.h"
#include <array>
#include <cstdint>
#include <vector>

namespace game
{

// 丸い生き物(model::buildCreature で作ったモデル)の動きの調整値(ツール creature2glb が、クリップに焼き込む)。tools/creatures/*.lua の
// walkFast / walkSlow / idle / attackStand / attackJump / death の表から読む。
// 角度はラジアン、長さはメートル、時間は秒
struct CreatureMotion
{
	// 地面に着いた瞬間(倒れ込み・跳びの着地)に、体が上下に潰れ(高さが縮み、前後・左右へ広がる)、減衰するバネのように戻る「びたーん」。
	// 潰れの量 c(高さを縮める割合)は、着いた瞬間に amount、半周期(0.3×duration)ごとに、逆向きに bounce 倍になって揺れ、duration で0に戻る
	struct Squash
	{
		float amount = 0.0f;   // 潰れの深さ(高さを縮める割合。0で潰れない)
		float stretch = 0.5f;  // 前後・左右へ広げる割合(高さを縮めた割合に対して。0.5でおよそ体積が変わらない)
		float duration = 0.4f; // 元へ戻りきるまでの時間
		float bounce = 0.3f;   // 跳ね返り(戻ったあと、逆に縦へ伸びる量の、潰れの量に対する割合)
	};
	// 歩き: 対角の足が同時に出る(前左+後右、前右+後左を交互。3対なら、前左・中右・後左と、前右・中左・後右の三脚歩行)。足ごとに、接地して後ろへ送る間と、浮かせて前へ戻す間を繰り返す。
	// 早歩きと、ゆっくり歩きの2通り(個体ごとに、どちらかで歩く)
	struct Walk
	{
		float speed = 1.4f;    // 進む速さ(m/秒)
		float stride = 0.6f;   // 1周期(全部の足が1歩ずつ)で進む距離。足の運びの速さは、速さ÷これ
		float duty = 0.55f;    // 1周期のうち、足が接地している割合
		float swing = 0.35f;   // 付け根で、足を前後に振る角度
		float lift = 0.3f;     // 前へ戻す間に、付け根で足を持ち上げる角度
		float kneeLift = 0.4f; // 同、ひざを曲げる角度
		float bob = 0.012f;    // 胴体の上下の弾み(m。1歩ごと)
		float hop = 0.0f;      // 弾みの形: 0で滑らかな波、1で地面で跳ね返る形(ひょこひょこ)
		float nod = 0.0f;      // 1歩ごとに、胴体を前後に傾ける角度
		float sway = 0.01f;    // 胴体の左右の揺れ(m)
		float roll = 0.04f;    // 胴体の左右の傾き
	};
	Walk walkFast, walkSlow;
	// 待機: 胴体が呼吸のように上下し、前足が少し動く
	struct Idle
	{
		float period = 2.8f;      // 呼吸の周期
		float breath = 0.012f;    // 胴体の上下(m)
		float pitch = 0.03f;      // 胴体の前後の傾き
		float frontSwing = 0.12f; // 前足を振る角度
	} idle;
	// 攻撃A: 後ろ足の付け根を支点に起き上がり → 前足をわきわき → 前へ倒れ込む(顔から地面へ) → 止まる → 起き上がって戻る
	struct Stand
	{
		float rise = 0.5f;        // 起き上がる時間
		float angle = 0.785f;     // 起き上がる角度(胴体の傾き)
		float frontRaise = 0.4f;  // 起き上がったときに、前足を付け根で前へ上げる角度
		float frontFold = 0.6f;   // 同、ひざを曲げる角度
		float wiggle = 1.0f;      // わきわきの時間
		float wiggleCount = 4.0f; // わきわきの回数
		float wiggleSwing = 0.35f; // わきわきで、前足を付け根で振る角度(左右は逆向き)
		float wiggleKnee = 0.4f;  // 同、ひざを曲げ伸ばしする角度
		float fall = 0.4f;        // 倒れ込む時間
		float fallEase = 2.5f;    // 倒れ込みの加速(ease-in の指数。大きいほど、最後に勢いがつく)
		float fallAngle = -0.35f; // 倒れたときの胴体の傾き(負で前が下がる。顔が地面に着くまで)
		float reach = 1.2f;       // 倒れ込みで、前足を付け根で前へ伸ばす角度
		float reachOpen = 0.4f;   // 同、外へ開く角度
		float reachKnee = -0.3f;  // 同、ひざの角度(負でひざを伸ばす)
		float bounce = 0.08f;     // 倒れた直後に、胴体が跳ね返る角度
		float hold = 0.6f;        // 倒れたまま止まる時間
		float recover = 0.7f;     // 起き上がって戻る時間
		Squash squash;            // 顔から地面に着いた瞬間の潰れ
	} stand;
	// 攻撃B: 溜め(体を沈める) → 伸び上がって前へ跳ぶ → 足を広げて、お腹から着地 → 止まる → 戻る。その場の姿勢だけ(跳ぶ軌道(着地点・頂点の高さ・
	// 滞空時間)は、ゲームが res/lua/data/enemies.lua の jump の値で決め、空中の区間を、滞空時間に合わせて伸縮して再生する)
	struct Jump
	{
		float crouch = 0.45f;     // 溜めの時間
		float crouchDepth = 0.1f; // 溜めで、胴体を沈める深さ(m)
		float crouchKnee = 0.35f; // 溜めで、ひざを曲げる角度
		float crouchPitch = -0.08f; // 溜めの胴体の傾き(負で前が下がる)
		float launch = 0.1f;      // 伸び上がる時間(地面を蹴る)
		float air = 0.6f;         // 空中の動きの長さ(クリップの時刻。ゲームは、実際の滞空時間に合わせて、この区間を伸縮して再生する)
		float airPitch = 0.25f;   // 跳び上がったときの胴体の傾き(着地までに0へ戻る)
		float spread = 1.15f;     // 着地で、足を付け根で外へ持ち上げる角度(手足を投げ出す)
		float spreadSwing = 0.45f; // 同、前足を前へ・後ろ足を後ろへ振る角度
		float bounce = 0.06f;     // 着地の直後に、胴体が揺れる角度
		float hold = 0.6f;        // 着地したまま止まる時間
		float recover = 0.8f;     // 起き上がって戻る時間
		float airStretch = 0.0f;  // 跳んでいる間に、縦へ伸びる割合(跳び立ち・着地の前ほど大きく、いちばん高い所で小さく)
		Squash squash;            // 着地の瞬間の潰れ
	} jump;
	// 倒れ: 手足が縮み、横へ倒れる(いまは使う場面が無い)
	struct Death
	{
		float duration = 0.8f;
		float roll = 1.5f;  // 横へ倒れる角度
		float curl = 1.1f;  // 手足を縮める角度
	} death;
};

// 丸い生き物の、手続き的なアニメーション(モーションのデータは使わず、数式で姿勢を作る)。ツール creature2glb が、クリップに焼き込むのに使う
// (ゲームは、焼き込んだクリップを CreatureClipAnimator で再生する)。
// 手足はボーンの名前(<name>_<left|right>_hip/knee/ankle/foot)で探し、付け根の前後の位置で、前から順に並べる(いちばん前の1対が前足、いちばん後ろの1対が後ろ足)。
// 付け根を振る・持ち上げる回転の軸、ひざ・足首を曲げる軸は、休止ポーズの関節の位置から求める(手足の形によらず、足先が前へ・上へ動く向き)。
// 足先と胴体(胴体のボーンに付く頂点)のいちばん低い所が地面に着くよう、胴体の高さを合わせ、接地している足は、付け根の持ち上げの角度で
// 足先を地面へ下ろす(IKは使わない。小さい角度の近似)
class CreatureAnimator : public CreatureDriver
{
public:
	// skeleton(休止ポーズ)のボーンの名前から、胴体(body)・手足を探す。vertices(休止ポーズ)のうち、胴体のボーンだけに付く頂点を、
	// 胴体が地面にめり込まないかの判定に使う。metersToModel は、メートル→モデルの単位
	CreatureAnimator(const model::Skeleton &skeleton, const std::vector<model::ModelVertex> &vertices, const CreatureMotion &motion, float metersToModel);

	size_t legCount() const { return legs_.size(); }
	float duration(Motion motion) const override;
	JumpPhases jumpPhases() const override;
	// 倒れ込みで0→1、戻りで1→0
	float fallen(const State &state) const override;
	BodyScale bodyScale(const State &state) const override;
	void apply(model::Skeleton &skeleton, const State &state) const override;

protected:
	float stride(Motion motion) const override;

private:
	struct Leg
	{
		int hip = -1, knee = -1, ankle = -1, foot = -1;
		model::Vec3 swingAxis, liftAxis, kneeAxis, ankleAxis; // 局所の回転の軸(休止ポーズのモデルの座標)
		float reach = 1.0f;      // 付け根から足先までの、水平の距離(モデルの単位)
		float walkOffset = 0.0f; // 歩きの位相のずれ
		bool front = false;      // いちばん前の1対
		bool rear = false;       // いちばん後ろの1対
		int side = 0;            // 0=左、1=右
	};
	struct LegPose
	{
		float swing = 0.0f, lift = 0.0f, knee = 0.0f, ankle = 0.0f;
		float raise = 0.0f, open = 0.0f; // 付け根の、X軸まわり(+で前の骨が上を向く)・Y軸まわり(+で外へ開く)の回転
		float planted = 1.0f; // 接地している(1)か、浮いている(0)か。接地している足は、足先を地面へ下ろす
		float level = 0.0f;   // 胴体の前後の傾きを、付け根で打ち消す割合(1で、胴体が傾いても足は休止ポーズの向きのまま)
	};
	static constexpr size_t kMaxLegs = 12;
	struct Pose
	{
		std::array<LegPose, kMaxLegs> legs;
		float pitch = 0.0f, roll = 0.0f;           // 胴体の前後(+で前が上がる)・左右の傾き
		float pivot = 0.0f;                        // 前後の傾きの支点: 0で胴体の中心、1で後ろ足の付け根
		float bob = 0.0f, sway = 0.0f, lunge = 0.0f; // 胴体の上下・左右・前への動き(m)
	};

	// 攻撃Aの、時刻ごとの進み具合
	struct StandPhase
	{
		float rise = 0.0f; // 起き上がり(0→1)
		float fall = 0.0f; // 倒れ込み(0→1。ease-in)
		float back = 1.0f; // 戻り(1→0)
		float fallEnd = 0.0f; // 顔から地面に着く時刻
	};
	StandPhase standPhase(float t) const;
	Pose pose(Motion motion, const State &state) const;
	void walkPose(const CreatureMotion::Walk &walk, const State &state, Pose &pose) const;
	void idlePose(const State &state, Pose &pose) const;
	void standPose(const State &state, Pose &pose) const;
	void jumpPose(const State &state, Pose &pose) const;
	void deathPose(const State &state, Pose &pose) const;
	const CreatureMotion::Walk *walkOf(Motion motion) const;
	void setLegs(model::Skeleton &skeleton, const Pose &pose, const std::array<float, kMaxLegs> &drop) const;

	std::vector<Leg> legs_;
	std::vector<model::Vec3> hull_; // 胴体のボーンだけに付く頂点(休止ポーズ。間引いたもの)。胴体のいちばん低い所を求める
	int body_ = -1;
	model::Vec3 bodyRest_, rearPivot_; // 胴体のボーンの位置、後ろ足の付け根の真ん中(休止ポーズ)
	CreatureMotion motion_;
	float toModel_ = 1.0f;
};

} // namespace game

#endif // CREATURE_CREATUREANIMATOR_H_
