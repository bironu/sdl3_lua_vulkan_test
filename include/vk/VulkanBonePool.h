#if !defined(VULKANBONEPOOL_H_)
#define VULKANBONEPOOL_H_

#include "misc/Uncopyable.h"
#include "vk/VulkanContext.h"
#include <vulkan/vulkan.h>
#include <memory>

// スキニング用(storage buffer)のdescriptor setを払い出すプール。VulkanTexturePoolと同じ考え方で、
// ウィンドウが1つ持ち、モデル(VulkanModel)が確保したsetを保持する。プールはshared_ptrで共有され、
// 最後の持ち主が手放すまで破棄されない。バインディングは binding 0=ボーン行列、1=頂点モーフの移動量(どちらもスキニングのコンピュートシェーダーが読む)、2=材質ごとの色・MToonの値(頂点シェーダーが読む)の storage buffer
class VulkanBonePool
{
public:
	UNCOPYABLE(VulkanBonePool);
	// maxSets=モデルのフレーム枠ごとのsetの数、maxPartSets=部品(材質ごとのメッシュ)のフレーム枠ごとのスキニング用setの数
	VulkanBonePool(std::shared_ptr<VulkanContext> ctx, uint32_t maxSets, uint32_t maxPartSets);
	~VulkanBonePool();

	bool isValid() const { return pool_ != VK_NULL_HANDLE; }
	VkDescriptorSetLayout layout() const { return layout_; }
	// 空きが無ければVK_NULL_HANDLE
	VkDescriptorSet allocate();
	void free(VkDescriptorSet set);
	// スキニング(コンピュート)用の部品ごとのset: binding 0=入力の頂点(休止ポーズ)、1=出力の頂点(スキニング済み)のstorage buffer
	VkDescriptorSetLayout partLayout() const { return partLayout_; }
	VkDescriptorSet allocatePart();
	void freePart(VkDescriptorSet set);

private:
	std::shared_ptr<VulkanContext> ctx_;
	VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
	VkDescriptorPool pool_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout partLayout_ = VK_NULL_HANDLE;
	VkDescriptorPool partPool_ = VK_NULL_HANDLE;
};

#endif // VULKANBONEPOOL_H_
