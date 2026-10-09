#if !defined(COMMON_FIELDRENDERER_H_)
#define COMMON_FIELDRENDERER_H_

#include "field/FieldMap.h"
#include "geo/Matrix.h"
#include "vk/VulkanMaterial.h"
#include "vk/VulkanMesh.h"
#include <memory>
#include <vector>

namespace SDL_
{
class VulkanWindow;
}

namespace game
{

// 地面(field::FieldMap: タイル+頂点の高さ)を描く。フィールドを kChunkSize マス四方のチャンクに分け、チャンクごと・タイルの種類ごとに1つのメッシュにする
// (チャンクの外れは、メッシュの範囲でカリングされる)。法線は頂点の高さからなめらかに求める。色は、タイルの定義(color)の単色。
// FieldMapを変えたら invalidate() で変えた範囲を伝え、update() で、その範囲のチャンクだけ作り直す(エディタ向け。ゲームはコンストラクタだけでよい)
class FieldRenderer
{
public:
	static constexpr int kChunkSize = 16;

	FieldRenderer(SDL_::VulkanWindow &window, const field::FieldMap &map, const std::vector<field::TileDef> &tiles);

	// マス(x0,z0)〜(x1,z1)(両端を含む)が変わった。法線が隣へ及ぶ分は、中で足す
	void invalidate(int x0, int z0, int x1, int z1);
	// 全部作り直す(サイズが違うフィールドを読んだとき。作り直したマップを渡す)
	void invalidateAll();
	// 変わったチャンクのメッシュを作り直す
	void update(const field::FieldMap &map);

	// 描画の予約
	void draw(const geo::Matrix4x4f &viewProj) const;

private:
	void build(const field::FieldMap &map, int chunkX, int chunkZ);

	SDL_::VulkanWindow &window_;
	std::vector<field::TileDef> tiles_;
	std::vector<VulkanMaterial> materials_; // タイルの種類ごと
	int chunksX_ = 0, chunksZ_ = 0;
	std::vector<std::vector<std::shared_ptr<VulkanMesh>>> meshes_; // [チャンク][タイルの種類]。無ければnullptr
	std::vector<bool> dirty_;
};

} // namespace game

#endif // COMMON_FIELDRENDERER_H_
