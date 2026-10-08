#if !defined(GAME_GAMESETTINGS_H_)
#define GAME_GAMESETTINGS_H_

#include <string>

namespace game
{

// GameSceneの調整値。res/lua/data/game_settings.lua から読む(読めない項目は、ここの既定のまま)。F5で読み直せる。
// 長さの単位はメートル、時間は秒。カメラの高さ・距離は、背の高さ referenceHeight のモデルの値で、他のモデルには背の高さに比例して拡大縮小する
struct GameSettings
{
	struct Player
	{
		float radius = 0.3f;         // 壁・置物との当たりの半径
		float walkSpeed = 1.6f;      // 歩きの速さ(m/秒。歩きのモーションの足運びに合わせた値)
		float slowRunSpeed = 3.1f;   // Slow Runの速さ(モーションが進む距離2.29m ÷ 0.73秒)
		float fastRunSpeed = 5.7f;   // Fast Runの速さ(3.02m ÷ 0.53秒)
		float climbSpeed = 0.5f;     // 歩いて登れない少し急な坂を、ゆっくり登る速さ
		float rollDistance = 4.5f;   // Stand To Rollで前へ転がる距離
		float rollStartOffset = 0.2f; // 転がり始めに、モーションの頭(助走・かがみ)をこの秒数(モーションの元の時間)だけ飛ばして、すぐ飛び込む
		float rollCancelProgress = 0.92f; // 転がって進んだ割合(0〜1)がこれを超えて、立ち上がる間に、スティックを倒していたら、立ち上がりを切り上げて、歩き・走りへつなぐ
		float rollRate = 2.0f;       // 転がるモーションの再生速度の倍率(1でモーションの元の速さ。大きいほど素早く転がる)
		float rollMotionTravel = 4.52f; // Stand To Rollのモーションが、元々進む距離(変換時のログの値。進み方の形を求めるのに使う。転がる距離を変えるには rollDistance を変える)
		float turnSpeed = 12.0f;     // キャラが進む向きへ向く速さ(ラジアン/秒の目安)
	} player;
	struct Climb
	{
		float tiltFactor = 0.3f;     // 坂を登るとき、キャラを坂の角度のこの割合だけ(0〜1。1で坂に垂直)、坂に沿って傾ける。0で傾けない
		float tiltSmooth = 8.0f;     // 傾きが目標へ追いつく速さ(1/秒。大きいほど速い)
		float blendLow = 0.45f;      // 登りのモーションを混ぜ始める勾配(高さ/距離)。これ以下は、歩き・走りだけ
		float blendHigh = 0.90f;     // 登りのモーションだけになる勾配。歩ける上限(maxSlope 0.85)あたり。この間は、なめらかに混ぜる
		float riseRate = 12.0f;      // 勾配が上がったとき、登りの混ざり具合が追いつく速さ(1/秒。大きいほど速い。急な坂へは素早く)
		float fallRate = 2.5f;       // 勾配が下がったとき、歩き・走りへ戻る速さ(1/秒。小さいほどゆっくり。唐突に立ち上がらないよう、遅め)
	} climb;
	struct Motion
	{
		float seamThreshold = 2.0f;  // ループする(立ち・歩き・走り・登り)モーションの、最初と最後の姿勢の差(骨の回転の差の合計。ラジアン)がこれを超えたら、折り返しを混ぜてつなぐ
		float loopBlend = 0.8f;      // その折り返しを混ぜる長さ(秒。モーションの長さの40%まで)。ループの周期が、この分だけ短くなる
	} motion;
	struct Input
	{
		float runStick = 0.85f;      // 左スティックをこれ以上傾けると「最大」(走り始める)。スティックが最大まで倒れ切らなくても走れるよう、1より低くしてある
		float runExit = 0.70f;       // 走っている間は、これを下回るまで走り続ける(走り始めより低く、境目で歩きに戻らないよう)
		float runGrace = 0.12f;      // スティックの値が一瞬 runExit を下回っても、この時間(秒)は走り続ける(スティックの接触不良・ぶれ対策)
		float moveEnter = 0.25f;     // 左スティックをこれ以上傾けると、歩き出す(それより小さい傾きは、遊びとして無視する)
		float moveExit = 0.15f;      // 歩いている間は、これを下回るまで歩き続ける
		float tapTime = 0.25f;       // Aボタンを、これより短く押して離したら単押し(転がる)。これ以上押し続けたら長押し(全力で走る)
		float rollOnPress = 1.0f;    // 1: Aボタンを押した瞬間に転がり始める(押しっぱなしにしても転がる。その後も押し続けて、スティックを最大にしていれば、Fast Run)。0: 離したときに、単押しなら転がる(長押しは転がらず、Fast Run)
		float triggerOn = 0.5f;      // L2/R2を押したとみなすトリガーの値
	} input;
	struct Camera
	{
		float referenceHeight = 1.6f; // 下の長さが、背の高さがこの値のモデルのもの
		float height = 1.35f;         // 注視点(プレイヤーの頭のあたり)の高さ
		float minEyeHeight = 0.3f;    // カメラの、地面からの最低の高さ。これより下へ行きそうなら、体の近くへ寄って、体に沿って頭の上へ回る
		float headTop = 1.9f;         // 体に沿って上がったカメラが、最後に着く高さ(頭のてっぺんの上)
		float distance = 3.5f;        // カメラの距離の初期値
		float minDistance = 1.2f;     // ホイールで寄れる最短の距離
		float maxDistance = 8.0f;     // 同、最長
		float yawSpeed = 2.6f;        // 右スティックを倒しきったときの、水平の回転の速さ(ラジアン/秒)
		float pitchSpeed = 1.6f;      // 同、上下
		float minArm = 0.5f;          // 地形に遮られたときの、頭からカメラまでの最短の距離
		float armRecover = 5.0f;      // 遮りが無くなったときに、元の距離へ戻る速さ(1/秒。大きいほど速い)
		float bodyDistance = 0.7f;    // 真上へ向けるとき、カメラが体に沿って上がる、体の中心からの水平の距離
		float approachFraction = 0.35f; // 真上へ向ける動きのうち、体の近くへ寄る(低いまま近づく)のに使う割合。残りで体に沿って上がる
	} camera;
	struct Fade
	{
		float toAction = 0.10f;      // クロスフェード: アクション(転がる・攻撃)を始めるとき(反応を遅らせないよう短く)
		float fromAction = 0.30f;    // アクションが終わって、止まったまま立ち・歩き・走りへ戻るとき
		float fromActionMoving = 0.15f; // アクションが終わる(切り上げる)とき、スティックを倒していて、そのまま歩き・走りへつなぐとき(短めにして、止まって見えないように)
		float locomotion = 0.20f;    // 立ち・歩き・走りの間
	} fade;
};

// 設定を読む(パスはリポジトリ直下からの相対。ファイルが無い・読めない項目は、既定のまま。失敗はログに出す)
GameSettings loadGameSettings(const std::string &relativePath = "res/lua/data/game_settings.lua");

} // namespace game

#endif // GAME_GAMESETTINGS_H_
