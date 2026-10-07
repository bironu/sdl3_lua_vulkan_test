#include "vk/VulkanTexture.h"
#include "vk/VulkanBuffer.h"
#include "sdl/SDLImage.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

VulkanTexture::VulkanTexture(std::shared_ptr<VulkanContext> ctx, const SDL_::Image& image)
	: ctx_(std::move(ctx))
{
	VkDevice device = ctx_->device();

	// RGBA8(メモリ上のバイト順R,G,B,A)へ変換し、行パディングを除いて詰める
	SDL_Surface* rgba = image.isEnabled() ? SDL_ConvertSurface(image.get(), SDL_PIXELFORMAT_RGBA32) : nullptr;
	if(!rgba){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Texture image convert failed. %s", SDL_GetError());
		return;
	}
	const uint32_t w = static_cast<uint32_t>(rgba->w);
	const uint32_t h = static_cast<uint32_t>(rgba->h);
	width_ = w;
	height_ = h;
	std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
	for(uint32_t y = 0; y < h; ++y){
		std::memcpy(pixels.data() + static_cast<size_t>(y) * w * 4,
			static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch, static_cast<size_t>(w) * 4);
	}
	SDL_DestroySurface(rgba);
	for(size_t i = 3; i < pixels.size(); i += 4){
		if(pixels[i] < 250){
			hasAlpha_ = true;
			break;
		}
	}

	VulkanBuffer staging(ctx_, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	if(!staging.isValid() || !staging.write(pixels.data(), pixels.size())){
		return;
	}

	// ミップマップ: 1x1まで縮小するレベル数。リニアblitに対応しないフォーマットなら1(ミップ無し)
	VkFormatProperties fp;
	vkGetPhysicalDeviceFormatProperties(ctx_->physicalDevice(), VK_FORMAT_R8G8B8A8_UNORM, &fp);
	const VkFormatFeatureFlags blitFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT
		| VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	const uint32_t mipLevels = ((fp.optimalTilingFeatures & blitFeatures) == blitFeatures)
		? static_cast<uint32_t>(std::bit_width(std::max(w, h))) : 1;

	// 画像(DEVICE_LOCAL)
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent = {w, h, 1};
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if(vkCreateImage(device, &imageInfo, nullptr, &image_) != VK_SUCCESS){
		return;
	}
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, image_, &req);
	const uint32_t type = ctx_->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if(type == UINT32_MAX){
		return;
	}
	VkMemoryAllocateInfo alloc{};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = type;
	if(vkAllocateMemory(device, &alloc, nullptr, &memory_) != VK_SUCCESS
		|| vkBindImageMemory(device, image_, memory_, 0) != VK_SUCCESS){
		return;
	}

	// 転送: 全レベルをUNDEFINED→TRANSFER_DSTにし、レベル0へコピー。
	// 以降、レベルi-1をTRANSFER_SRCにして半分のサイズでレベルiへblit(リニア縮小)し、
	// 使い終えたレベルi-1をSHADER_READ_ONLYへ。最後のレベルだけTRANSFER_DSTのまま残るので最後に遷移する
	const bool transferred = ctx_->submitOneShot([&](VkCommandBuffer cmd){
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image_;
		barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};

		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy region{};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {w, h, 1};
		vkCmdCopyBufferToImage(cmd, staging.get(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		int32_t mipW = static_cast<int32_t>(w);
		int32_t mipH = static_cast<int32_t>(h);
		for(uint32_t i = 1; i < mipLevels; ++i){
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			const int32_t nextW = std::max(mipW / 2, 1);
			const int32_t nextH = std::max(mipH / 2, 1);
			VkImageBlit blit{};
			blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
			blit.srcOffsets[1] = {mipW, mipH, 1};
			blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
			blit.dstOffsets[1] = {nextW, nextH, 1};
			vkCmdBlitImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			mipW = nextW;
			mipH = nextH;
		}

		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
	});
	if(!transferred){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Texture upload failed.");
		return;
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image_;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = imageInfo.format;
	viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
	if(vkCreateImageView(device, &viewInfo, nullptr, &view_) != VK_SUCCESS){
		return;
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = static_cast<float>(mipLevels);
	// 異方性フィルタ: デバイスが対応していれば斜めから見たときのぼけ/ちらつきを抑える
	if(ctx_->maxAnisotropy() > 0.0f){
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = std::min(ctx_->maxAnisotropy(), 8.0f);
	}
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	if(vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS){
		sampler_ = VK_NULL_HANDLE;
	}
}

VulkanTexture::~VulkanTexture()
{
	VkDevice device = ctx_->device();
	if(pool_){ pool_->free(set_); }
	if(sampler_){ vkDestroySampler(device, sampler_, nullptr); }
	if(view_){ vkDestroyImageView(device, view_, nullptr); }
	if(image_){ vkDestroyImage(device, image_, nullptr); }
	if(memory_){ vkFreeMemory(device, memory_, nullptr); }
}

bool VulkanTexture::bind(std::shared_ptr<VulkanTexturePool> pool)
{
	if(!isValid() || !pool || set_){
		return false;
	}
	const VkDescriptorSet set = pool->allocate();
	if(!set){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Texture descriptor pool exhausted.");
		return false;
	}
	pool_ = std::move(pool);
	set_ = set;

	// 3つのbindingすべてに同じ画像を書く(MToonの影の色・発光の画像を持たない普通のテクスチャ用)
	VkDescriptorImageInfo imageInfo{sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet writes[VulkanTexturePool::kBindingCount]{};
	for(uint32_t i = 0; i < VulkanTexturePool::kBindingCount; ++i){
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = set_;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].pImageInfo = &imageInfo;
	}
	vkUpdateDescriptorSets(ctx_->device(), VulkanTexturePool::kBindingCount, writes, 0, nullptr);
	return true;
}
