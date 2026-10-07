#if !defined(COMMON_PROPRENDERER_H_)
#define COMMON_PROPRENDERER_H_

#include "field/FieldMap.h"
#include "field/PropCollision.h"
#include "geo/Matrix.h"
#include "vk/VulkanModel.h"
#include <map>
#include <memory>
#include <string>

class ResourceSet;
namespace SDL_
{
class VulkanWindow;
}

namespace game
{

// フィールドに置いた物(field::PlacedProp)を描く。モデル(res/prop/ の .glb)は、名前ごとに1つだけGPUに作って、置いた数だけ変換行列を変えて描く
// (最初に使うときに読む。読めない名前は、ログを1回出して、描かない)。メインスレッドで使う
class PropRenderer
{
public:
	PropRenderer(SDL_::VulkanWindow &window, ResourceSet &resources);

	// 置いた物を全部描く(描画の予約)
	void draw(const field::FieldMap &map, const geo::Matrix4x4f &viewProj);
	// 1つだけ描く(エディタの、置く前の見本など)
	void drawOne(const std::string &name, float x, float y, float z, float yaw, float scale, const geo::Matrix4x4f &viewProj);
	// 名前の物の、足元の中心から見た水平の半径(拡大率1のとき。選択の当たりに使う)。読めなければ0.5
	float radius(const std::string &name);
	// 名前の物の、足元からてっぺんまでの高さ(拡大率1のとき)
	float height(const std::string &name) { return entry(name).height; }
	// 名前の物の、足元の当たりの長方形(読めなければ valid=false)
	const field::PropFootprint &footprint(const std::string &name) { return entry(name).footprint; }
	// 事前に読んでおく(省略可)
	void preload(const std::string &name) { entry(name); }

private:
	struct Entry
	{
		std::shared_ptr<VulkanModel> model;
		float radius = 0.5f;
		float height = 1.0f;
		field::PropFootprint footprint;
	};
	const Entry &entry(const std::string &name);

	SDL_::VulkanWindow &window_;
	ResourceSet &resources_;
	std::map<std::string, Entry> entries_;
};

} // namespace game

#endif // COMMON_PROPRENDERER_H_
