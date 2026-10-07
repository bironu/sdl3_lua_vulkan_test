#if !defined(VULKANTEXTURESET_H_)
#define VULKANTEXTURESET_H_

#include "misc/Uncopyable.h"
#include "vk/VulkanContext.h"
#include "vk/VulkanTexture.h"
#include "vk/VulkanTexturePool.h"
#include <vulkan/vulkan.h>
#include <memory>

// MToonの材質用の、複数のテクスチャを1組にしたdescriptor set(set=1。binding 0=基本の色、1=影の色、2=発光)。
// 影の色・発光が無いときは、基本の色の画像で埋める(使うかどうかは、材質のフラグでシェーダーが決める)。
// 組んだテクスチャは、このsetが破棄されるまで生かしておく
class VulkanTextureSet
{
public:
	UNCOPYABLE(VulkanTextureSet);
	// baseは必須。shade/emissiveはnullptrでもよい。作れなければnullptr
	static std::shared_ptr<VulkanTextureSet> create(const std::shared_ptr<VulkanTexturePool> &pool,
		const std::shared_ptr<VulkanTexture> &base, const std::shared_ptr<VulkanTexture> &shade,
		const std::shared_ptr<VulkanTexture> &emissive);
	~VulkanTextureSet();

	VkDescriptorSet descriptorSet() const { return set_; }

private:
	VulkanTextureSet() = default;
	std::shared_ptr<VulkanTexturePool> pool_;
	std::shared_ptr<VulkanTexture> textures_[VulkanTexturePool::kBindingCount];
	VkDescriptorSet set_ = VK_NULL_HANDLE;
};

#endif // VULKANTEXTURESET_H_
