#if !defined(COMMON_MODELFACTORY_H_)
#define COMMON_MODELFACTORY_H_

#include "vk/VulkanModel.h"
#include <memory>
#include <string>

class ResourceSet;
namespace SDL_
{
class VulkanWindow;
}

namespace game
{

// VRMのパスから、描画できるモデル(GPUの資源)を作る。メインスレッドで呼ぶ。
// データ(ModelData・埋め込み画像のデコード)は、resourcesから取る: 別スレッドでResourceSet::modelWithImages()済みなら、キャッシュから速く取れる。
// 作れなければnullptr
std::shared_ptr<VulkanModel> createVulkanModel(SDL_::VulkanWindow &window, ResourceSet &resources, const std::string &path);

} // namespace game

#endif // COMMON_MODELFACTORY_H_
