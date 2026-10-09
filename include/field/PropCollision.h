#if !defined(FIELD_PROPCOLLISION_H_)
#define FIELD_PROPCOLLISION_H_

#include "field/FieldMap.h"
#include "model/ModelData.h"
#include <functional>
#include <string>
#include <vector>

namespace field
{

// 置物のモデルの、足元の水平な広がり(モデルの座標・エンジンの右手系での、長方形)。キャラが歩いて当たる高さ(足元から sliceTop まで)の部分だけで求める
// (木の枝葉・ビルの庇のように、高い所の出っ張りでは、幹や壁の足元を塞がない)。その高さに何も無ければ valid=false(当たらない)
struct PropFootprint
{
	bool valid = false;
	float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
	float top = 0.0f; // モデル全体のいちばん高い所(足元から。跳び越えられるかの判定に使う)
};
PropFootprint computeFootprint(const model::ModelData &data, float sliceTop = 1.0f);

// 置いた物(FieldMap::props)の足元の長方形(置いた向き・大きさで回転・拡大したもの)への、円(キャラ)の当たり。
// 物を空間分割(格子)に入れて、近くのものだけ調べる(物が数千あっても、動く物ごとの判定は軽い)
class PropCollision
{
public:
	using FootprintFn = std::function<PropFootprint(const std::string &name)>;

	// フィールドの置物から作る(置物を変えたら作り直す)
	void build(const FieldMap &map, const FootprintFn &footprint);
	// 中心(x, z)・半径radiusの円が物にめり込んでいたら、押し出す。動いたらtrue(隣り合う物のために、何度か繰り返す)
	bool resolve(float &x, float &z, float radius) const;
	// 円が物に当たっているか(押し出さない)
	bool overlaps(float x, float z, float radius) const;
	// 円が当たっている物のうち、いちばん高いてっぺんの高さ(ワールド。当たっている物が無ければ、float の最小値)
	float topAt(float x, float z, float radius) const;

	struct Box
	{
		float x = 0.0f, z = 0.0f;       // 置いた位置
		float cosYaw = 1.0f, sinYaw = 0.0f;
		float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f; // 向きを揃えた(回転前の)長方形。拡大済み
		float top = 0.0f; // てっぺんの高さ(ワールド)
	};
	const std::vector<Box> &boxes() const { return boxes_; }

private:
	static constexpr float kGridSize = 8.0f;
	bool pushOut(const Box &box, float &x, float &z, float radius) const;
	template<typename Fn>
	void forNear(float x, float z, float radius, Fn &&fn) const;

	std::vector<Box> boxes_;
	int gridX_ = 0, gridZ_ = 0;
	std::vector<std::vector<int>> grid_;
};

} // namespace field

#endif // FIELD_PROPCOLLISION_H_
