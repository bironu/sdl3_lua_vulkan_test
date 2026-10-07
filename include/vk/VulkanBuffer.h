#if !defined(VULKANBUFFER_H_)
#define VULKANBUFFER_H_

#include "misc/Uncopyable.h"
#include "vk/VulkanContext.h"
#include <vulkan/vulkan.h>
#include <memory>

// VkBuffer + VkDeviceMemory。CPUから書き込めるメモリ(HOST_VISIBLE|HOST_COHERENT)に確保する。
// Apple Siliconのようなユニファイドメモリなら十分。専用GPUで頻繁に読むデータは、
// 将来ステージングバッファ経由でDEVICE_LOCALへ転送する形にする
class VulkanBuffer
{
public:
	UNCOPYABLE(VulkanBuffer);
	// gpuOnly=trueなら、GPUだけが使うDEVICE_LOCALのメモリに確保する(CPUからはwriteできない。GPUが書いてGPUが読むバッファ用。ユニファイドメモリでも、GPU向けに最適化された配置になる)
	VulkanBuffer(std::shared_ptr<VulkanContext> ctx, VkDeviceSize size, VkBufferUsageFlags usage, bool gpuOnly = false);
	~VulkanBuffer();

	bool isValid() const { return buffer_ != VK_NULL_HANDLE; }
	VkBuffer get() const { return buffer_; }
	VkDeviceSize size() const { return size_; }

	// dataのsizeバイトを先頭から書き込む(size <= buffer size)
	bool write(const void* data, VkDeviceSize size);

private:
	std::shared_ptr<VulkanContext> ctx_;
	VkBuffer buffer_ = VK_NULL_HANDLE;
	VkDeviceMemory memory_ = VK_NULL_HANDLE;
	VkDeviceSize size_ = 0;
};

#endif // VULKANBUFFER_H_
