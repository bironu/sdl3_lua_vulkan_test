#if !defined(VULKANTEXTURE_H_)
#define VULKANTEXTURE_H_

#include "misc/Uncopyable.h"
#include "vk/VulkanContext.h"
#include "vk/VulkanTexturePool.h"
#include <vulkan/vulkan.h>
#include <memory>

// SDL_::Imageから作る2Dテクスチャ(DEVICE_LOCALのVkImage + view + sampler)。
// ピクセルはRGBA8(UNORM)に変換され、ステージングバッファ経由で転送される。
// ミップマップは転送時にvkCmdBlitImageで全レベル生成する(非対応フォーマットなら1レベル)。
// サンプラーはトリリニア+異方性フィルタ(デバイスが対応する場合、最大8x)。
// 生成後はSHADER_READ_ONLY_OPTIMALレイアウトで、フラグメントシェーダーから参照できる
namespace SDL_
{
class Image;
}

class VulkanTexture
{
public:
	UNCOPYABLE(VulkanTexture);
	VulkanTexture(std::shared_ptr<VulkanContext> ctx, const SDL_::Image& image);
	~VulkanTexture();

	bool isValid() const { return sampler_ != VK_NULL_HANDLE; }
	// 元画像のピクセルサイズ
	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }
	// 半透明のピクセル(アルファ<255)を含むか。描画順(半透明は不透明のあとに奥から)の判定に使う
	bool hasAlpha() const { return hasAlpha_; }

	// poolからdescriptor set(set=1, binding=0)を確保して、このテクスチャを書き込む。
	// 以降descriptorSet()で描画時にバインドできる。破棄時にsetはプールへ返却される
	bool bind(std::shared_ptr<VulkanTexturePool> pool);
	VkDescriptorSet descriptorSet() const { return set_; }
	VkImageView view() const { return view_; }
	VkSampler sampler() const { return sampler_; }

private:
	std::shared_ptr<VulkanContext> ctx_;
	VkImage image_ = VK_NULL_HANDLE;
	VkDeviceMemory memory_ = VK_NULL_HANDLE;
	VkImageView view_ = VK_NULL_HANDLE;
	VkSampler sampler_ = VK_NULL_HANDLE;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	bool hasAlpha_ = false;
	std::shared_ptr<VulkanTexturePool> pool_;
	VkDescriptorSet set_ = VK_NULL_HANDLE;
};

#endif // VULKANTEXTURE_H_
