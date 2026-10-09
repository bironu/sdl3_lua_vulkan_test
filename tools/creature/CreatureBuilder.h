#if !defined(CREATURE_CREATUREBUILDER_H_)
#define CREATURE_CREATUREBUILDER_H_

#include "model/ModelData.h"
#include <memory>
#include <string>
#include <vector>

namespace model
{

// 手続き的に作る、丸い生き物の形: ふっくらした卵形の胴体の前面に顔(閉じ目の弧と口)、細い棒の手足(ツール creature2glb だけが使う。ゲームは書き出した glb を読む)。
// 長さの単位は任意(ふつうはメートル。描画の側で大きさを合わせる)、角度はラジアン。
// 向きの言葉: 前(forward)はモデルの正面(元の座標系の-Z)、外(out)は体の中心から左右の外側、上(up)は+Y
struct CreatureSpec
{
	// 胴体: 卵形。前後の長さ・幅・高さは、いちばん太い所の直径
	struct Body
	{
		float length = 1.0f;
		float width = 0.8f;
		float height = 0.78f;
		float taper = 0.05f; // 前を細く、後ろを太くする割合(0で左右対称の楕円体。1未満)
		int slices = 22;     // 前後の分割数
		int sides = 22;      // 周の分割数
	} body;
	// 顔: 胴体の前面に、表面に沿って少し浮かせて貼る部品(テクスチャは使わずメッシュで作る)。
	// 位置は、顔の中心(胴体の中心から、正面を上へ pitch だけ回した向きの表面の点)からの、表面に沿った ずれ(外・上。長さ)
	struct Face
	{
		float pitch = -0.05f;    // 顔の中心の向き(正面から上へ。負なら下)
		float lift = 0.004f;     // 部品を表面から浮かせる距離
		// 閉じ目: 上に凸の弧(「^」)の細い筒。左右に1つずつ
		float eyeSpacing = 0.11f; // 目の中心の、顔の中心からの外へのずれ
		float eyeUp = 0.07f;      // 同、上へのずれ(弧の円の中心)
		float eyeRadius = 0.05f;  // 弧の半径
		float eyeSpan = 2.0f;     // 弧の角度(πで半円)
		float eyeThickness = 0.011f; // 筒の太さ(半径)
		int eyeSegments = 10;
		// 口: 縦長の楕円の板
		float mouthUp = -0.07f;   // 口の中心の、顔の中心からの上へのずれ
		float mouthWidth = 0.045f; // 幅・高さ(直径)
		float mouthHeight = 0.07f;
		int mouthSides = 16;
	} face;
	// 手足の筒の作り
	struct LegMesh
	{
		int sides = 8;        // 周の分割数
		int rings = 6;        // 区間ごとのリングの数(関節の近くほど細かく並べる)
		float blend = 0.3f;   // 関節で、2つのボーンの重みを混ぜ、筒を丸める幅(前後の区間の短い方の長さに対する割合。0〜0.45)
		float inset = 1.5f;   // 付け根の筒を、胴体の中へ入り込ませる長さ(付け根の太さの倍数)
	} legMesh;
	// 手足の1対(左右対称)。付け根の位置は、胴体の表面で決め、関節の位置は、1つ前の関節からのずれ(外・上・前)で決める
	struct Leg
	{
		std::string name = "leg"; // ボーンの名前の頭(<name>_left_hip など)
		float along = 0.0f;       // 付け根の前後の位置: 胴体の後ろの端 -1 〜 前の端 +1
		float angle = -0.6f;      // 付け根の周の位置: 真横 0、下へ負、上へ正
		float knee[3] = {0.15f, 0.05f, 0.0f};  // 付け根→ひざ(外・上・前)
		float ankle[3] = {0.03f, -0.36f, 0.0f}; // ひざ→足首
		float foot[3] = {0.0f, -0.04f, 0.04f};  // 足首→足先
		float radius[4] = {0.034f, 0.03f, 0.027f, 0.022f}; // 太さ(半径): 付け根・ひざ・足首・足先
	};
	std::vector<Leg> legs; // 前から後ろの順でなくてよい(アニメーションは付け根の位置で前後を決める)
	// 色(rgb 0〜1)
	struct Colors
	{
		float back[3] = {0.95f, 0.92f, 0.82f};  // 胴体の背側
		float belly[3] = {0.98f, 0.95f, 0.87f}; // 胴体の腹側
		float legs[3] = {0.80f, 0.76f, 0.68f};  // 手足
		float face[3] = {0.08f, 0.07f, 0.07f};  // 目・口
	} color;
	float bellyLine = -0.3f; // 背と腹の境目: 面の向き(法線)の上下の成分がこれより上なら背の色(-1〜1)
};

// CreatureSpec から、モデルデータ(頂点・材質・ボーン)を作る。テクスチャは無く、材質の拡散色だけ。
// 座標系はPMXと同じ(左手系: +Yが上、モデルの正面が-Z。左の手足が+X)。休止ポーズで、いちばん低い足先が Y=0 に着く。
// ボーン: root(原点)、body(胴体の中心。顔もこれに付く)、手足ごとに <name>_<left|right>_hip/knee/ankle/foot(foot は足先の端。重みは持たない)。
// 休止ポーズの回転は単位(局所の回転の軸は、モデルの座標軸のまま)
std::shared_ptr<ModelData> buildCreature(const CreatureSpec &spec, const std::string &name = "creature");

} // namespace model

#endif // CREATURE_CREATUREBUILDER_H_
