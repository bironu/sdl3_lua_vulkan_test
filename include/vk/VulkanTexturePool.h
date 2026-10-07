#if !defined(VULKANTEXTUREPOOL_H_)
#define VULKANTEXTUREPOOL_H_

#include "misc/Uncopyable.h"
#include "vk/VulkanContext.h"
#include <vulkan/vulkan.h>
#include <memory>

// テクスチャ(combined image sampler)用のdescriptor setを払い出すプール。
// ウィンドウごとに1つ持ち、ウィンドウが作ったテクスチャ(VulkanTexture)が確保したsetを保持する。
// テクスチャが先にウィンドウを超えて生き残っても安全なよう、プールはshared_ptrで共有され、
// 最後の持ち主が手放すまで破棄されない
class VulkanTexturePool
{
public:
	UNCOPYABLE(VulkanTexturePool);
	VulkanTexturePool(std::shared_ptr<VulkanContext> ctx, uint32_t maxTextures);
	~VulkanTexturePool();

	bool isValid() const { return pool_ != VK_NULL_HANDLE; }
	const std::shared_ptr<VulkanContext> &context() const { return ctx_; }
	// set=1 の combined image sampler(フラグメントシェーダー)。binding 0=基本の画像、1=MToonの影の色の画像、2=MToonの発光の画像
	// (普通のテクスチャは3つとも同じ画像にしてあるので、どのシェーダーからも使える)
	static constexpr uint32_t kBindingCount = 3;
	VkDescriptorSetLayout layout() const { return layout_; }
	// 空きが無ければVK_NULL_HANDLE
	VkDescriptorSet allocate();
	void free(VkDescriptorSet set);

private:
	std::shared_ptr<VulkanContext> ctx_;
	VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
	VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

#endif // VULKANTEXTUREPOOL_H_
