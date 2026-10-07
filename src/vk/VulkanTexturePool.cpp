#include "vk/VulkanTexturePool.h"

VulkanTexturePool::VulkanTexturePool(std::shared_ptr<VulkanContext> ctx, uint32_t maxTextures)
	: ctx_(std::move(ctx))
{
	VkDevice device = ctx_->device();

	VkDescriptorSetLayoutBinding bindings[kBindingCount]{};
	for(uint32_t i = 0; i < kBindingCount; ++i){
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = kBindingCount;
	layoutInfo.pBindings = bindings;
	if(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS){
		layout_ = VK_NULL_HANDLE;
		return;
	}

	VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxTextures * kBindingCount};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // テクスチャ破棄時に個別に返却する
	poolInfo.maxSets = maxTextures;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &size;
	if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS){
		pool_ = VK_NULL_HANDLE;
	}
}

VulkanTexturePool::~VulkanTexturePool()
{
	VkDevice device = ctx_->device();
	if(pool_){ vkDestroyDescriptorPool(device, pool_, nullptr); }
	if(layout_){ vkDestroyDescriptorSetLayout(device, layout_, nullptr); }
}

VkDescriptorSet VulkanTexturePool::allocate()
{
	if(!isValid()){
		return VK_NULL_HANDLE;
	}
	VkDescriptorSetAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	info.descriptorPool = pool_;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &layout_;
	VkDescriptorSet set = VK_NULL_HANDLE;
	if(vkAllocateDescriptorSets(ctx_->device(), &info, &set) != VK_SUCCESS){
		return VK_NULL_HANDLE;
	}
	return set;
}

void VulkanTexturePool::free(VkDescriptorSet set)
{
	if(set){
		vkFreeDescriptorSets(ctx_->device(), pool_, 1, &set);
	}
}
