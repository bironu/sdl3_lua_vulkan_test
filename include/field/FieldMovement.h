#if !defined(FIELD_FIELDMOVEMENT_H_)
#define FIELD_FIELDMOVEMENT_H_

#include "field/FieldMap.h"
#include <vector>

namespace field
{

// 地面の上の歩行の制限: 歩けないタイル(水など。TileDef::walkable)と、急すぎる勾配(maxSlope)へは進めない。置物(PropCollision)とは別に、地面だけで決まる
struct MovementRules
{
	float maxSlope = 0.85f;
	std::vector<bool> tileWalkable; // タイルの番号ごと。範囲外の番号は歩ける

	static MovementRules fromTiles(const std::vector<TileDef> &tiles, float maxSlope);
};

// 半径radiusの円が、(x, z)に立てるか(円の中心と、上下左右の端が、歩けるタイルの上。フィールドの外は見ない: 縁は呼び出し側で止める)
bool canStandAt(const FieldMap &map, const MovementRules &rules, float x, float z, float radius);

// (x, z) から (toX, toZ) へ動く。そのまま進めなければ、x方向だけ・z方向だけを試して、壁に沿って滑る。
// 急な勾配は、動く向きの登りの勾配(高さの差/距離)で見る(等高線に沿った動きと、下りは通る)。動く前から立てない所(水の中・出られない崖)にいるときは、
// タイルの制限は掛けない(出られるように)。動いたらtrue
bool moveOnField(const FieldMap &map, const MovementRules &rules, float radius, float &x, float &z, float toX, float toZ);

} // namespace field

#endif // FIELD_FIELDMOVEMENT_H_
