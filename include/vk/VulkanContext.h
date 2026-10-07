#if !defined(VULKANCONTEXT_H_)
#define VULKANCONTEXT_H_

#include "misc/Uncopyable.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>

// Vulkanのインスタンス・物理デバイス・論理デバイス・キューを保持する。
// 全VulkanWindowで共有する(shared_ptrで持ち、最後のウィンドウが消えてから破棄される)。
// 物理デバイスの選択にはサーフェスが必要なため、2段階で初期化する:
//   1. initInstance()        ウィンドウ生成前
//   2. initDevice(surface)   最初のVulkanWindowのサーフェス作成後
class VulkanContext
{
public:
	UNCOPYABLE(VulkanContext);
	VulkanContext() = default;
	~VulkanContext();

	bool initInstance();
	// サーフェスへ描画(present)できるグラフィックスキューを持つデバイスを選んで作成
	bool initDevice(VkSurfaceKHR surface);

	bool hasDevice() const { return device_ != VK_NULL_HANDLE; }
	VkInstance instance() const { return instance_; }
	VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
	VkDevice device() const { return device_; }
	uint32_t queueFamily() const { return queueFamily_; }
	// 異方性フィルタ: 有効化できたか、その最大値(無効なら0)
	float maxAnisotropy() const { return maxAnisotropy_; }
	// キューブマップ配列(samplerCubeArray)を使えるか。点光源のシャドウマップに必要
	bool hasCubeArray() const { return hasCubeArray_; }
	// グラフィックスとpresentを兼ねるキュー
	VkQueue queue() const { return queue_; }

	// typeBitsに含まれ、propsを全て満たすメモリタイプのindex。無ければUINT32_MAX
	uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

	// 一時コマンドバッファにrecord()で記録して即submitし、完了まで待つ(リソース転送などの初期化用)
	bool submitOneShot(const std::function<void(VkCommandBuffer)>& record) const;

private:
	VkInstance instance_ = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
	VkDevice device_ = VK_NULL_HANDLE;
	uint32_t queueFamily_ = 0;
	VkQueue queue_ = VK_NULL_HANDLE;
	float maxAnisotropy_ = 0.0f;
	bool hasCubeArray_ = false;
};

#endif // VULKANCONTEXT_H_
