#include "vk/VulkanTextureSet.h"
#include <SDL3/SDL_log.h>

std::shared_ptr<VulkanTextureSet> VulkanTextureSet::create(const std::shared_ptr<VulkanTexturePool> &pool,
	const std::shared_ptr<VulkanTexture> &base, const std::shared_ptr<VulkanTexture> &shade,
	const std::shared_ptr<VulkanTexture> &emissive)
{
	if(!pool || !base || !base->isValid()){
		return nullptr;
	}
	std::shared_ptr<VulkanTextureSet> result(new VulkanTextureSet());
	result->set_ = pool->allocate();
	if(!result->set_){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Texture descriptor pool exhausted (texture set).");
		return nullptr;
	}
	result->pool_ = pool;
	result->textures_[0] = base;
	result->textures_[1] = shade && shade->isValid() ? shade : base;
	result->textures_[2] = emissive && emissive->isValid() ? emissive : base;

	VkDescriptorImageInfo infos[VulkanTexturePool::kBindingCount];
	VkWriteDescriptorSet writes[VulkanTexturePool::kBindingCount]{};
	for(uint32_t i = 0; i < VulkanTexturePool::kBindingCount; ++i){
		infos[i] = {result->textures_[i]->sampler(), result->textures_[i]->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = result->set_;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].pImageInfo = &infos[i];
	}
	vkUpdateDescriptorSets(pool->context()->device(), VulkanTexturePool::kBindingCount, writes, 0, nullptr);
	return result;
}

VulkanTextureSet::~VulkanTextureSet()
{
	if(pool_ && set_){
		pool_->free(set_);
	}
}
