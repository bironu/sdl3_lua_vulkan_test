#include "vk/VulkanBonePool.h"

VulkanBonePool::VulkanBonePool(std::shared_ptr<VulkanContext> ctx, uint32_t maxSets, uint32_t maxPartSets)
	: ctx_(std::move(ctx))
{
	VkDevice device = ctx_->device();

	VkDescriptorSetLayoutBinding bindings[3]{};
	for(uint32_t i = 0; i < 3; ++i){ // 0=ボーン行列、1=頂点モーフの移動量、2=材質ごとの色
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT; // 描画(材質)とスキニング(ボーン・モーフ)の両方から読む
	}
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 3;
	layoutInfo.pBindings = bindings;
	if(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS){
		layout_ = VK_NULL_HANDLE;
		return;
	}

	VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets * 3};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // モデル破棄時に個別に返却する
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &size;
	if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS){
		pool_ = VK_NULL_HANDLE;
		return;
	}

	// 部品(材質ごとのメッシュ)ごとのスキニング用: 0=入力の頂点(休止ポーズ)、1=出力の頂点(スキニング済み)
	VkDescriptorSetLayoutBinding partBindings[2]{};
	for(uint32_t i = 0; i < 2; ++i){
		partBindings[i].binding = i;
		partBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		partBindings[i].descriptorCount = 1;
		partBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = partBindings;
	if(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &partLayout_) != VK_SUCCESS){
		partLayout_ = VK_NULL_HANDLE;
		return;
	}
	size.descriptorCount = maxPartSets * 2;
	poolInfo.maxSets = maxPartSets;
	if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &partPool_) != VK_SUCCESS){
		partPool_ = VK_NULL_HANDLE;
	}
}

VulkanBonePool::~VulkanBonePool()
{
	VkDevice device = ctx_->device();
	if(partPool_){ vkDestroyDescriptorPool(device, partPool_, nullptr); }
	if(partLayout_){ vkDestroyDescriptorSetLayout(device, partLayout_, nullptr); }
	if(pool_){ vkDestroyDescriptorPool(device, pool_, nullptr); }
	if(layout_){ vkDestroyDescriptorSetLayout(device, layout_, nullptr); }
}

VkDescriptorSet VulkanBonePool::allocate()
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

void VulkanBonePool::free(VkDescriptorSet set)
{
	if(set){
		vkFreeDescriptorSets(ctx_->device(), pool_, 1, &set);
	}
}

VkDescriptorSet VulkanBonePool::allocatePart()
{
	if(!partPool_){
		return VK_NULL_HANDLE;
	}
	VkDescriptorSetAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	info.descriptorPool = partPool_;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &partLayout_;
	VkDescriptorSet set = VK_NULL_HANDLE;
	if(vkAllocateDescriptorSets(ctx_->device(), &info, &set) != VK_SUCCESS){
		return VK_NULL_HANDLE;
	}
	return set;
}

void VulkanBonePool::freePart(VkDescriptorSet set)
{
	if(set){
		vkFreeDescriptorSets(ctx_->device(), partPool_, 1, &set);
	}
}
