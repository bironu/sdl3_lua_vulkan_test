#if !defined(MODEL_PHYSICS_H_)
#define MODEL_PHYSICS_H_

#include "model/ModelData.h"
#include "model/Skeleton.h"
#include <memory>

namespace model
{

// モデルの物理演算(PMXの剛体とジョイント。髪やスカートなどの揺れ)。Bulletを使う(Bulletのヘッダーはここに出さない)。
// モデルの元の座標系(PMX: 左手系)の数値のまま計算する(MMDも同じ数値をBulletへ渡す)。
// 使い方: skeleton.update()でアニメーションを解いた後に step() を呼ぶと、
//   ボーンに追従する剛体をそのボーンの姿勢へ動かし → 物理演算を進め → 物理演算で動くボーンの姿勢をskeletonへ書き戻す
class Physics
{
public:
	// 作れなければ(剛体が無いなど)nullptr
	static std::unique_ptr<Physics> create(const ModelData &data, const Skeleton &skeleton);
	~Physics();

	// dt秒だけ進める(内部は1/60秒刻み)
	void step(Skeleton &skeleton, float dt);
	// 剛体を現在のボーンの姿勢へ戻して、速度を0にする(モーションの頭出しや大きく動いた直後に呼ぶ。揺れが暴れるのを防ぐ)
	void reset(const Skeleton &skeleton);

	struct Impl; // 実装(Bulletの世界)。Physics.cppの中だけで使う

private:
	Physics();
	std::unique_ptr<Impl> impl_;
};

} // namespace model

#endif // MODEL_PHYSICS_H_
