#include "vk/VulkanBuffer.h"
#include <SDL3/SDL_log.h>
#include <cstring>

VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanContext> ctx, VkDeviceSize size, VkBufferUsageFlags usage, bool gpuOnly)
	: ctx_(std::move(ctx))
	, size_(size)
{
	VkDevice device = ctx_->device();

	VkBufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(vkCreateBuffer(device, &info, nullptr, &buffer_) != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateBuffer failed.");
		buffer_ = VK_NULL_HANDLE;
		return;
	}

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, buffer_, &req);
	const VkMemoryPropertyFlags wanted = gpuOnly ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t type = ctx_->findMemoryType(req.memoryTypeBits, wanted);
	if(type == UINT32_MAX){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No suitable memory type for buffer.");
	}
	else{
		VkMemoryAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = type;
		if(vkAllocateMemory(device, &alloc, nullptr, &memory_) == VK_SUCCESS
			&& vkBindBufferMemory(device, buffer_, memory_, 0) == VK_SUCCESS){
			return;
		}
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate/bind buffer memory.");
	}
	// 失敗: 作りかけを破棄してisValid()==falseにする
	if(memory_){
		vkFreeMemory(device, memory_, nullptr);
		memory_ = VK_NULL_HANDLE;
	}
	vkDestroyBuffer(device, buffer_, nullptr);
	buffer_ = VK_NULL_HANDLE;
}

VulkanBuffer::~VulkanBuffer()
{
	VkDevice device = ctx_->device();
	if(buffer_){ vkDestroyBuffer(device, buffer_, nullptr); }
	if(memory_){ vkFreeMemory(device, memory_, nullptr); }
}

bool VulkanBuffer::write(const void* data, VkDeviceSize size)
{
	if(!isValid() || size > size_){
		return false;
	}
	void* mapped = nullptr;
	if(vkMapMemory(ctx_->device(), memory_, 0, size, 0, &mapped) != VK_SUCCESS){
		return false;
	}
	std::memcpy(mapped, data, static_cast<size_t>(size));
	vkUnmapMemory(ctx_->device(), memory_);
	return true;
}
