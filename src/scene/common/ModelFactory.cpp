#include "scene/common/ModelFactory.h"
#include "resources/ResourceSet.h"
#include "sdl/SDLVulkanWindow.h"

namespace game
{

std::shared_ptr<VulkanModel> createVulkanModel(SDL_::VulkanWindow &window, ResourceSet &resources, const std::string &path)
{
	const auto data = resources.model(path);
	if(!data){
		return nullptr;
	}
	const auto &embedded = data->embeddedImages;
	return VulkanModel::create(window.getContext(), window.getBonePool(), window.getTexturePool(), data,
		[&](const std::string &imagePath){
			return window.createCachedTexture(imagePath, resources.image(imagePath));
		}, SDL_::VulkanWindow::kFrameSlots,
		[&](const std::vector<uint8_t> &bytes, bool ignoreAlpha){
			// bytesはモデルのembeddedImagesの要素。その番号の、デコード済みの画像を使う
			const size_t index = static_cast<size_t>(&bytes - embedded.data());
			return window.createTexture(resources.modelImage(path, index, ignoreAlpha));
		});
}

} // namespace game
