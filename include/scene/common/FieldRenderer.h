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

// 地面のタイル(field::FieldMap)を描く。タイルの種類ごとに、そのタイルのマスを全部まとめた1つのメッシュにして、種類の数だけの描画で済ませる(マスごとには描かない)。
// 色は、タイルの定義(color)の単色。FieldMapを変えたら、作り直すこと(コンストラクタで作る)
class FieldRenderer
{
public:
	FieldRenderer(SDL_::VulkanWindow &window, const field::FieldMap &map, const std::vector<field::TileDef> &tiles);

	// 描画の予約
	void draw(SDL_::VulkanWindow &window, const geo::Matrix4x4f &viewProj) const;

private:
	struct Layer
	{
		std::shared_ptr<VulkanMesh> mesh;
		VulkanMaterial material;
	};
	std::vector<Layer> layers_;
};

} // namespace game

#endif // COMMON_FIELDRENDERER_H_
