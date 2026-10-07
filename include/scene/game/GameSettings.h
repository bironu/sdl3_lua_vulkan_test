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
		float climbEnter = 0.85f;    // 先の勾配が、歩ける上限のこの割合を超えたら、登るモーションにする(境目で歩きと登りが入れ替わって止まらないよう、少し手前から)
		float rollDistance = 4.5f;   // Stand To Rollで前へ転がる距離(モーションの長さの間に、等速で進む)
		float turnSpeed = 12.0f;     // キャラが進む向きへ向く速さ(ラジアン/秒の目安)
	} player;
	struct Input
	{
		float runStick = 0.95f;      // 左スティックをこれ以上傾けると「最大」(走る)
		float tapTime = 0.25f;       // Aボタンを、これより短く押して離したら単押し(転がる)。これ以上押し続けたら長押し(全力で走る)
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
		float fromAction = 0.30f;    // アクションが終わって、立ち・歩き・走りへ戻るとき
		float locomotion = 0.20f;    // 立ち・歩き・走りの間
	} fade;
};

// 設定を読む(パスはリポジトリ直下からの相対。ファイルが無い・読めない項目は、既定のまま。失敗はログに出す)
GameSettings loadGameSettings(const std::string &relativePath = "res/lua/data/game_settings.lua");

} // namespace game

#endif // GAME_GAMESETTINGS_H_
