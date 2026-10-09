#include "sdl/SDLVulkanWindow.h"
#include "sdl/SDLImage.h"
#include "vk/VulkanMath.h"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <string>

namespace SDL_
{

VulkanWindow::VulkanWindow(std::shared_ptr<VulkanContext> ctx, const char* title, int x, int y, int w, int h, Uint32 flags)
	: Window(title, x, y, w, h, flags | SDL_WINDOW_VULKAN)
	, ctx_(std::move(ctx))
{
	if(!isWindow()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindow(Vulkan) failed. %s", SDL_GetError());
		return;
	}
	if(!SDL_Vulkan_CreateSurface(get(), ctx_->instance(), nullptr, &surface_)){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_Vulkan_CreateSurface failed. %s", SDL_GetError());
		return;
	}
	if(!ctx_->hasDevice() && !ctx_->initDevice(surface_)){
		return;
	}

	VkDevice device = ctx_->device();
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = ctx_->queueFamily();
	if(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS){
		return;
	}
	ready_ = createDescriptors() && createFrames() && createSwapchain();
}

VulkanWindow::~VulkanWindow()
{
	if(ctx_ && ctx_->hasDevice()){
		VkDevice device = ctx_->device();
		vkDeviceWaitIdle(device);
		destroySwapchain();
		for(const auto& p : scenePipelines_){
			for(VkPipeline pipeline : {p.lit, p.litSkin, p.litOpaque, p.litSkinOpaque, p.litMask, p.litSkinMask, p.outline, p.outlineSkin, p.outlineOpaque, p.outlineSkinOpaque, p.sprite2d, p.sprite3d}){
				if(pipeline){ vkDestroyPipeline(device, pipeline, nullptr); }
			}
		}
		if(skinPipeline_){ vkDestroyPipeline(device, skinPipeline_, nullptr); }
		if(skinLayout_){ vkDestroyPipelineLayout(device, skinLayout_, nullptr); }
		if(postPipeline_){ vkDestroyPipeline(device, postPipeline_, nullptr); }
		if(postLayout_){ vkDestroyPipelineLayout(device, postLayout_, nullptr); }
		if(postPool_){ vkDestroyDescriptorPool(device, postPool_, nullptr); }
		if(postSetLayout_){ vkDestroyDescriptorSetLayout(device, postSetLayout_, nullptr); }
		if(postSampler_){ vkDestroySampler(device, postSampler_, nullptr); }
		if(postRenderPass_){ vkDestroyRenderPass(device, postRenderPass_, nullptr); }
		if(renderPassMsaa_){ vkDestroyRenderPass(device, renderPassMsaa_, nullptr); }
		if(outlineLayout_){ vkDestroyPipelineLayout(device, outlineLayout_, nullptr); }
		if(outlineSkinLayout_){ vkDestroyPipelineLayout(device, outlineSkinLayout_, nullptr); }
		if(litSkinLayout_){ vkDestroyPipelineLayout(device, litSkinLayout_, nullptr); }
		if(shadowSkinPipeline_){ vkDestroyPipeline(device, shadowSkinPipeline_, nullptr); }
		if(shadowSkinLayout_){ vkDestroyPipelineLayout(device, shadowSkinLayout_, nullptr); }
		if(litLayout_){ vkDestroyPipelineLayout(device, litLayout_, nullptr); }
		if(spriteLayout_){ vkDestroyPipelineLayout(device, spriteLayout_, nullptr); }
		if(shadowPipeline_){ vkDestroyPipeline(device, shadowPipeline_, nullptr); }
		if(shadowLayout_){ vkDestroyPipelineLayout(device, shadowLayout_, nullptr); }
		if(shadowRenderPass_){ vkDestroyRenderPass(device, shadowRenderPass_, nullptr); }
		if(shadowSampler_){ vkDestroySampler(device, shadowSampler_, nullptr); }
		if(renderPass_){ vkDestroyRenderPass(device, renderPass_, nullptr); }
		if(descriptorPool_){ vkDestroyDescriptorPool(device, descriptorPool_, nullptr); } // descriptor setも解放される
		if(descriptorSetLayout_){ vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr); }
		for(auto& f : frames_){
			for(VkFramebuffer fb : f.pointShadowFramebuffers){
				if(fb){ vkDestroyFramebuffer(device, fb, nullptr); }
			}
			for(VkImageView v : f.pointShadowLayerViews){
				if(v){ vkDestroyImageView(device, v, nullptr); }
			}
			if(f.pointShadowArrayView){ vkDestroyImageView(device, f.pointShadowArrayView, nullptr); }
			if(f.pointShadowImage){ vkDestroyImage(device, f.pointShadowImage, nullptr); }
			if(f.pointShadowMemory){ vkFreeMemory(device, f.pointShadowMemory, nullptr); }
			if(f.shadowFramebuffer){ vkDestroyFramebuffer(device, f.shadowFramebuffer, nullptr); }
			if(f.shadowView){ vkDestroyImageView(device, f.shadowView, nullptr); }
			if(f.shadowImage){ vkDestroyImage(device, f.shadowImage, nullptr); }
			if(f.shadowMemory){ vkFreeMemory(device, f.shadowMemory, nullptr); }
			if(f.inFlight){ vkDestroyFence(device, f.inFlight, nullptr); }
			if(f.queryPool){ vkDestroyQueryPool(device, f.queryPool, nullptr); }
			if(f.imageAvailable){ vkDestroySemaphore(device, f.imageAvailable, nullptr); }
		}
		if(commandPool_){ vkDestroyCommandPool(device, commandPool_, nullptr); } // コマンドバッファも解放される
	}
	if(surface_){
		SDL_Vulkan_DestroySurface(ctx_->instance(), surface_, nullptr);
	}
	// SDL_Windowは基底のデストラクタが破棄する
}

void VulkanWindow::draw(std::shared_ptr<VulkanMesh> mesh, const geo::Matrix4x4f& mvp, const geo::Matrix4x4f& model)
{
	draw(std::move(mesh), mvp, model, VulkanMaterial{nullptr, defaultSpecular_, defaultShininess_});
}

void VulkanWindow::draw(std::shared_ptr<VulkanMesh> mesh, const geo::Matrix4x4f& mvp, const geo::Matrix4x4f& model,
	const VulkanMaterial& material)
{
	auto texture = material.texture ? material.texture : texture_;
	if(mesh && mesh->isValid() && texture && texture->descriptorSet()){
		pending_.push_back({DrawKind::Lit, std::move(mesh), std::move(texture), mvp, model,
			{material.specular, material.shininess,
			// フラグ: bit0=影を受けるか、bit1〜7=自発光する点光源の番号+1(0なら通常)、bit8〜=環境光に足す明るさ*100。シェーダー側で分解する
			static_cast<float>((material.receiveShadow ? 1 : 0) + 2 * (material.emissiveLight + 1)
				+ 256 * std::clamp(static_cast<int>(material.ambientBoost * 100.0f + 0.5f), 0, 255)
				// bit16〜: アルファの切り抜きのしきい値*100(0なら切り抜き無し=ブレンド)
				+ 65536 * (material.alphaCutoff > 0.0f ? std::clamp(static_cast<int>(material.alphaCutoff * 100.0f + 0.5f), 1, 100) : 0)),
			material.alpha}, material.castShadow});
	}
}

void VulkanWindow::draw(const std::shared_ptr<VulkanModel>& model, const geo::Matrix4x4f& viewProj, const geo::Matrix4x4f& modelMatrix)
{
	if(!model){
		return;
	}
	const auto mvp = viewProj * modelMatrix;
	for(size_t i = 0; i < model->partCount(); ++i){
		const auto& part = model->part(i);
		const size_t before = pending_.size();
		draw(part.mesh, mvp, modelMatrix, part.material);
		if(pending_.size() > before){
			pending_.back().owner = model;
			pending_.back().partIndex = static_cast<uint32_t>(i);
			pending_.back().materialIndex = part.materialIndex;
			if(part.textureSet){
				pending_.back().textureSetOverride = part.textureSet->descriptorSet();
			}
			if(part.mesh->isSkinned()){
				pending_.back().kind = DrawKind::LitSkinned; // ボーン行列(boneSet)はswap()で決める
			}
		}
	}
}

void VulkanWindow::drawSprite2D(std::shared_ptr<VulkanTexture> texture, float x, float y, float w, float h,
	float r, float g, float b, float a)
{
	if(!texture || !texture->descriptorSet() || !quadScreen_){
		return;
	}
	// 論理画面座標(左上原点) → クリップ空間: proj * translate(x,y) * scale(w,h)
	const auto model = geo::createTranslation<float>(geo::Vector3f(x, y, 0.0f)) * geo::createScale<float>(geo::Vector3f(w, h, 1.0f));
	pending_.push_back({DrawKind::Sprite2D, quadScreen_, std::move(texture),
		vk_::createScreenProjection(screenWidth_, screenHeight_) * model, geo::createIdentityMatrix4x4<float>(), {r, g, b, a}});
}

void VulkanWindow::drawSprite2DRegion(std::shared_ptr<VulkanTexture> texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
	float r, float g, float b, float a)
{
	if(!texture || !texture->descriptorSet() || !quadScreen_){
		return;
	}
	const auto model = geo::createTranslation<float>(geo::Vector3f(x, y, 0.0f)) * geo::createScale<float>(geo::Vector3f(w, h, 1.0f));
	pending_.push_back({DrawKind::Sprite2D, quadScreen_, std::move(texture),
		vk_::createScreenProjection(screenWidth_, screenHeight_) * model, geo::createIdentityMatrix4x4<float>(), {r, g, b, a}});
	pending_.back().uvRect = {u0, v0, u1 - u0, v1 - v0};
}

void VulkanWindow::drawSprite2DRotated(std::shared_ptr<VulkanTexture> texture, float cx, float cy, float w, float h, float radians,
	float r, float g, float b, float a)
{
	if(!texture || !texture->descriptorSet() || !quadScreen_){
		return;
	}
	// quadScreenは0〜1の板。中心を原点へ → 大きさ → 回転 → 中心の位置へ
	const auto model = geo::createTranslation<float>(geo::Vector3f(cx, cy, 0.0f)) * geo::createRotationZ<float>(radians)
		* geo::createScale<float>(geo::Vector3f(w, h, 1.0f)) * geo::createTranslation<float>(geo::Vector3f(-0.5f, -0.5f, 0.0f));
	pending_.push_back({DrawKind::Sprite2D, quadScreen_, std::move(texture),
		vk_::createScreenProjection(screenWidth_, screenHeight_) * model, geo::createIdentityMatrix4x4<float>(), {r, g, b, a}});
}

void VulkanWindow::drawSprite3D(std::shared_ptr<VulkanTexture> texture, const geo::Matrix4x4f& mvp,
	float r, float g, float b, float a)
{
	if(!texture || !texture->descriptorSet() || !quadWorld_){
		return;
	}
	pending_.push_back({DrawKind::Sprite3D, quadWorld_, std::move(texture), mvp, geo::createIdentityMatrix4x4<float>(), {r, g, b, a}});
}

void VulkanWindow::destroySwapchain()
{
	VkDevice device = ctx_->device();
	for(VkFramebuffer fb : framebuffers_){
		vkDestroyFramebuffer(device, fb, nullptr);
	}
	framebuffers_.clear();
	destroySceneTargets();
	for(VkImageView v : imageViews_){
		vkDestroyImageView(device, v, nullptr);
	}
	imageViews_.clear();
	if(depthView_){ vkDestroyImageView(device, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
	if(depthImage_){ vkDestroyImage(device, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; }
	if(depthMemory_){ vkFreeMemory(device, depthMemory_, nullptr); depthMemory_ = VK_NULL_HANDLE; }
	for(VkSemaphore s : renderFinished_){
		vkDestroySemaphore(device, s, nullptr);
	}
	renderFinished_.clear();
	images_.clear();
	if(swapchain_){
		vkDestroySwapchainKHR(device, swapchain_, nullptr);
		swapchain_ = VK_NULL_HANDLE;
	}
}

bool VulkanWindow::createSwapchain()
{
	VkPhysicalDevice phys = ctx_->physicalDevice();
	VkDevice device = ctx_->device();

	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface_, &caps);

	// サイズ(High-DPI対応のためピクセル単位)
	if(caps.currentExtent.width != 0xFFFFFFFF){
		extent_ = caps.currentExtent;
	}
	else{
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(get(), &w, &h);
		extent_.width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
		extent_.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
	}
	if(extent_.width == 0 || extent_.height == 0){
		return true; // 最小化中。swap()が復帰を待つ
	}

	// フォーマット(BGRA8優先)
	uint32_t fmtCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface_, &fmtCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(fmtCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface_, &fmtCount, formats.data());
	if(formats.empty()){
		return false;
	}
	VkSurfaceFormatKHR chosen = formats[0];
	for(const auto& f : formats){
		if(f.format == VK_FORMAT_B8G8R8A8_UNORM){
			chosen = f;
			break;
		}
	}
	format_ = chosen.format;

	uint32_t imageCount = caps.minImageCount + 1;
	if(caps.maxImageCount > 0){
		imageCount = std::min(imageCount, caps.maxImageCount);
	}

	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if(!(caps.supportedCompositeAlpha & alpha)){
		alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
	}

	VkSwapchainCreateInfoKHR info{};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = surface_;
	info.minImageCount = imageCount;
	info.imageFormat = chosen.format;
	info.imageColorSpace = chosen.colorSpace;
	info.imageExtent = extent_;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = caps.currentTransform;
	info.compositeAlpha = alpha;
	info.presentMode = VK_PRESENT_MODE_FIFO_KHR; // 必ずサポートされる(VSync)
	info.clipped = VK_TRUE;
	if(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain_) != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateSwapchainKHR failed.");
		return false;
	}

	uint32_t count = 0;
	vkGetSwapchainImagesKHR(device, swapchain_, &count, nullptr);
	images_.resize(count);
	vkGetSwapchainImagesKHR(device, swapchain_, &count, images_.data());

	VkSemaphoreCreateInfo semInfo{};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	renderFinished_.resize(count);
	for(auto& s : renderFinished_){
		if(vkCreateSemaphore(device, &semInfo, nullptr, &s) != VK_SUCCESS){
			return false;
		}
	}

	// シーンの描画解像度は、論理画面のサイズ(setScreenSize。既定1920x1080)。ウィンドウのサイズと違えば、後処理が拡大縮小して出す
	renderExtent_ = {std::max(1u, static_cast<uint32_t>(screenWidth_ + 0.5f)), std::max(1u, static_cast<uint32_t>(screenHeight_ + 0.5f))};

	// 深度フォーマットを一度だけ選ぶ(シャドウマップと共通)
	if(!chooseDepthFormat()){
		return false;
	}
	if(!createDepthResources()){
		return false;
	}

	// レンダーパス/パイプラインはフォーマットが決まってから一度だけ作る
	// (ビューポートは動的ステートなので、リサイズ時の作り直しは不要)
	if(!renderPass_ && (!createRenderPass() || !createPipelines())){
		return false;
	}

	if(!createSceneTargets()){
		return false;
	}

	imageViews_.resize(count);
	framebuffers_.resize(count);
	for(uint32_t i = 0; i < count; ++i){
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = images_[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format_;
		viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		if(vkCreateImageView(device, &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS){
			return false;
		}
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = postRenderPass_;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &imageViews_[i];
		fbInfo.width = extent_.width;
		fbInfo.height = extent_.height;
		fbInfo.layers = 1;
		if(vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS){
			return false;
		}
	}
	return true;
}

bool VulkanWindow::createDescriptors()
{
	VkDevice device = ctx_->device();

	// set 0: フレームごと(すべてフラグメント)。binding 0=uniform buffer、binding 1=平行光源のシャドウマップ、
	//        binding 2=点光源のシャドウマップ(キューブマップ配列。層=光源の番号)
	VkDescriptorSetLayoutBinding bindings[3]{};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].descriptorCount = 1; // 全光源のキューブマップ配列(1つのサンプラー)
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 3;
	layoutInfo.pBindings = bindings;
	if(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS){
		return false;
	}

	const VkDescriptorPoolSize poolSizes[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFramesInFlight * 2}, // 平行光源のシャドウマップ + 点光源のキューブマップ配列
	};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = kMaxFramesInFlight;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS){
		return false;
	}

	// フレームごとにdescriptor setとuniform bufferを用意する
	std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts;
	layouts.fill(descriptorSetLayout_);
	std::array<VkDescriptorSet, kMaxFramesInFlight> sets;
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool_;
	allocInfo.descriptorSetCount = kMaxFramesInFlight;
	allocInfo.pSetLayouts = layouts.data();
	if(vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS){
		return false;
	}

	for(uint32_t i = 0; i < kMaxFramesInFlight; ++i){
		auto& f = frames_[i];
		f.descriptorSet = sets[i];
		f.uniformBuffer = std::make_unique<VulkanBuffer>(ctx_, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		if(!f.uniformBuffer->isValid()){
			return false;
		}
		VkDescriptorBufferInfo bufferInfo{f.uniformBuffer->get(), 0, sizeof(UniformData)};
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = f.descriptorSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo = &bufferInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	// シャドウマップ(画像・レンダーパス・フレームバッファ・サンプラー)を作り、各フレームのdescriptor setへ書き込む
	if(!createShadowResources()){
		return false;
	}

	// テクスチャ用のdescriptor setプール(set 1)
	texturePool_ = std::make_shared<VulkanTexturePool>(ctx_, 1024);
	if(!texturePool_->isValid()){
		return false;
	}

	// ボーン行列(スキニング用)のdescriptor setプール(set 2)。モデル1つにつきフレーム枠の数だけ使う。敵の大軍(GameSceneのEnemyHorde。数千体)と、
	// その読み直し(前の分が描画中のフレームで生きている間に、新しく作る)が収まる数
	bonePool_ = std::make_shared<VulkanBonePool>(ctx_, 8192, 8192);
	if(!bonePool_->isValid()){
		return false;
	}

	// スプライト用の板(2D: (0,0)-(1,1)で左上がuv(0,0)、3D: 中心原点の1x1)
	const uint32_t quadIndices[] = {0, 1, 2, 2, 3, 0};
	const vk_::Vertex screenVerts[] = {
		{{0.0f, 0.0f, 0.0f}, {1, 1, 1}, {0.0f, 0.0f}, {0, 0, 0}},
		{{1.0f, 0.0f, 0.0f}, {1, 1, 1}, {1.0f, 0.0f}, {0, 0, 0}},
		{{1.0f, 1.0f, 0.0f}, {1, 1, 1}, {1.0f, 1.0f}, {0, 0, 0}},
		{{0.0f, 1.0f, 0.0f}, {1, 1, 1}, {0.0f, 1.0f}, {0, 0, 0}},
	};
	const vk_::Vertex worldVerts[] = {
		{{-0.5f,  0.5f, 0.0f}, {1, 1, 1}, {0.0f, 0.0f}, {0, 0, 1}},
		{{ 0.5f,  0.5f, 0.0f}, {1, 1, 1}, {1.0f, 0.0f}, {0, 0, 1}},
		{{ 0.5f, -0.5f, 0.0f}, {1, 1, 1}, {1.0f, 1.0f}, {0, 0, 1}},
		{{-0.5f, -0.5f, 0.0f}, {1, 1, 1}, {0.0f, 1.0f}, {0, 0, 1}},
	};
	quadScreen_ = std::make_shared<VulkanMesh>(ctx_, screenVerts, 4, quadIndices, 6);
	quadWorld_ = std::make_shared<VulkanMesh>(ctx_, worldVerts, 4, quadIndices, 6);
	if(!quadScreen_->isValid() || !quadWorld_->isValid()){
		return false;
	}

	// テクスチャ未設定でもシェーダーが有効なように、1x1の白をデフォルトにする
	auto white = std::make_shared<SDL_::Image>(1, 1);
	if(!white->isEnabled()){
		return false;
	}
	white->fillRect(SDL_::Color(255, 255, 255, 255));
	return setTexture(white);
}

std::shared_ptr<VulkanTexture> VulkanWindow::createTexture(const std::shared_ptr<SDL_::Image> &image)
{
	if(!image || !texturePool_){
		return nullptr;
	}
	auto texture = std::make_shared<VulkanTexture>(ctx_, *image);
	if(!texture->isValid() || !texture->bind(texturePool_)){
		return nullptr;
	}
	return texture;
}

std::shared_ptr<VulkanTexture> VulkanWindow::createCachedTexture(const std::string &key, const std::shared_ptr<SDL_::Image> &image)
{
	const auto found = textureCache_.find(key);
	if(found != textureCache_.end()){
		if(auto alive = found->second.lock()){
			return alive;
		}
	}
	auto texture = createTexture(image);
	if(texture){
		textureCache_[key] = texture;
	}
	return texture;
}

bool VulkanWindow::setTexture(const std::shared_ptr<SDL_::Image> &image)
{
	auto texture = createTexture(image);
	if(!texture){
		return false;
	}
	vkDeviceWaitIdle(ctx_->device()); // 描画中の古いテクスチャを解放するため
	texture_ = std::move(texture);
	return true;
}

bool VulkanWindow::createFrames()
{
	VkDevice device = ctx_->device();
	std::array<VkCommandBuffer, kMaxFramesInFlight> buffers;
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool_;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = kMaxFramesInFlight;
	if(vkAllocateCommandBuffers(device, &allocInfo, buffers.data()) != VK_SUCCESS){
		return false;
	}

	VkSemaphoreCreateInfo semInfo{};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 初回のvkWaitForFencesを通す
	// GPUタイムスタンプが使えるか(使えなければ計測しない)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(ctx_->physicalDevice(), &props);
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(ctx_->physicalDevice(), &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(ctx_->physicalDevice(), &familyCount, families.data());
		const uint32_t bits = ctx_->queueFamily() < familyCount ? families[ctx_->queueFamily()].timestampValidBits : 0;
		if(bits > 0 && props.limits.timestampPeriod > 0.0f){
			timestampPeriod_ = props.limits.timestampPeriod;
			timestampMask_ = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
		}
		skinMaxGroupsX_ = std::max(1u, props.limits.maxComputeWorkGroupCount[0]);
		profile_ = SDL_getenv("VULKAN_PROFILE") != nullptr && timestampPeriod_ > 0.0f;
		// 起動時の設定(動作確認・比較用): VULKAN_AA=0〜3(None/MSAA 4x/FXAA/MSAA 4x + FXAA)、VULKAN_CULL=0でカリング無効
		if(const char* aa = SDL_getenv("VULKAN_AA")){
			antiAliasing_ = static_cast<AntiAliasing>(std::clamp(SDL_atoi(aa), 0, 3));
		}
		if(const char* interval = SDL_getenv("VULKAN_PSINT")){
			pointShadowInterval_ = std::max(1, SDL_atoi(interval)); // 点光源の影の更新間隔(1=毎フレーム)
		}
		if(const char* minPixels = SDL_getenv("VULKAN_OLMIN")){
			outlineMinPixels_ = static_cast<float>(SDL_atof(minPixels)); // 輪郭線を省く最小の太さ(ピクセル。0で無効)
		}
		if(const char* cutoff = SDL_getenv("VULKAN_PSCUT")){
			pointShadowCutoff_ = static_cast<float>(SDL_atof(cutoff)); // 点光源の影の絞り込み(0で無効)
		}
		if(const char* cull = SDL_getenv("VULKAN_CULL")){
			frustumCulling_ = SDL_atoi(cull) != 0;
		}
	}
	for(uint32_t i = 0; i < kMaxFramesInFlight; ++i){
		auto& f = frames_[i];
		f.commandBuffer = buffers[i];
		if(profile_){
			VkQueryPoolCreateInfo qInfo{};
			qInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			qInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			qInfo.queryCount = kTimestampCount;
			if(vkCreateQueryPool(device, &qInfo, nullptr, &f.queryPool) != VK_SUCCESS){
				profile_ = false;
			}
		}
		if(vkCreateSemaphore(device, &semInfo, nullptr, &f.imageAvailable) != VK_SUCCESS
			|| vkCreateFence(device, &fenceInfo, nullptr, &f.inFlight) != VK_SUCCESS){
			return false;
		}
	}
	return true;
}

bool VulkanWindow::chooseDepthFormat()
{
	if(depthFormat_ != VK_FORMAT_UNDEFINED){
		return true;
	}
	// 描画先(DEPTH_STENCIL_ATTACHMENT)にもシャドウマップとしての参照(SAMPLED_IMAGE)にも使える形式。D32_SFLOAT優先
	const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	for(VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}){
		VkFormatProperties fp;
		vkGetPhysicalDeviceFormatProperties(ctx_->physicalDevice(), f, &fp);
		if((fp.optimalTilingFeatures & need) == need){
			depthFormat_ = f;
			return true;
		}
	}
	SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No supported depth format.");
	return false;
}

bool VulkanWindow::createShadowResources()
{
	VkDevice device = ctx_->device();
	if(!chooseDepthFormat()){
		return false;
	}

	// デプスの値をそのまま読む(比較はシェーダー側で行う)ので、補間なしのサンプラー
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if(vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler_) != VK_SUCCESS){
		return false;
	}

	// デプスだけのレンダーパス。終了時のレイアウトは、メインのパスのフラグメントシェーダーから読めるDEPTH_STENCIL_READ_ONLY
	VkAttachmentDescription depth{};
	depth.format = depthFormat_;
	depth.samples = VK_SAMPLE_COUNT_1_BIT;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.pDepthStencilAttachment = &depthRef;
	VkSubpassDependency deps[2]{};
	// 前に同じ画像を読んでいたフラグメントシェーダーの完了を待ってから書く
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	// 書込み完了後にフラグメントシェーダーが読む
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	VkRenderPassCreateInfo rpInfo{};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpInfo.attachmentCount = 1;
	rpInfo.pAttachments = &depth;
	rpInfo.subpassCount = 1;
	rpInfo.pSubpasses = &subpass;
	rpInfo.dependencyCount = 2;
	rpInfo.pDependencies = deps;
	if(vkCreateRenderPass(device, &rpInfo, nullptr, &shadowRenderPass_) != VK_SUCCESS){
		return false;
	}

	// フレーム枠ごとのシャドウマップ
	for(auto& f : frames_){
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = depthFormat_;
		imageInfo.extent = {kShadowMapSize, kShadowMapSize, 1};
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if(vkCreateImage(device, &imageInfo, nullptr, &f.shadowImage) != VK_SUCCESS){
			return false;
		}
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(device, f.shadowImage, &req);
		const uint32_t type = ctx_->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if(type == UINT32_MAX){
			return false;
		}
		VkMemoryAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = type;
		if(vkAllocateMemory(device, &alloc, nullptr, &f.shadowMemory) != VK_SUCCESS
			|| vkBindImageMemory(device, f.shadowImage, f.shadowMemory, 0) != VK_SUCCESS){
			return false;
		}
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = f.shadowImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = depthFormat_;
		viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
		if(vkCreateImageView(device, &viewInfo, nullptr, &f.shadowView) != VK_SUCCESS){
			return false;
		}
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = shadowRenderPass_;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &f.shadowView;
		fbInfo.width = kShadowMapSize;
		fbInfo.height = kShadowMapSize;
		fbInfo.layers = 1;
		if(vkCreateFramebuffer(device, &fbInfo, nullptr, &f.shadowFramebuffer) != VK_SUCCESS){
			return false;
		}

		// このフレーム枠のdescriptor set(set 0, binding 1)へ
		VkDescriptorImageInfo imageDescriptor{shadowSampler_, f.shadowView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = f.descriptorSet;
		write.dstBinding = 1;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageDescriptor;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		// 点光源のシャドウマップ: 全光源分(kMaxPointLights個 x 6面)を1枚の配列画像にする。
		// 1面ずつ、同じレンダーパス/パイプラインで描く(層 = 光源の番号*6 + 面)
		if(!ctx_->hasCubeArray()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "This device does not support cube map arrays (imageCubeArray).");
			return false;
		}
		VkImageCreateInfo cubeInfo = imageInfo;
		cubeInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		cubeInfo.extent = {kPointShadowSize, kPointShadowSize, 1};
		cubeInfo.arrayLayers = Frame::kPointShadowLayers;
		if(vkCreateImage(device, &cubeInfo, nullptr, &f.pointShadowImage) != VK_SUCCESS){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cube shadow map array image creation failed.");
			return false;
		}
		vkGetImageMemoryRequirements(device, f.pointShadowImage, &req);
		const uint32_t cubeType = ctx_->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if(cubeType == UINT32_MAX){
			return false;
		}
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = cubeType;
		if(vkAllocateMemory(device, &alloc, nullptr, &f.pointShadowMemory) != VK_SUCCESS
			|| vkBindImageMemory(device, f.pointShadowImage, f.pointShadowMemory, 0) != VK_SUCCESS){
			return false;
		}
		for(uint32_t layer = 0; layer < Frame::kPointShadowLayers; ++layer){
			VkImageViewCreateInfo layerView{};
			layerView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			layerView.image = f.pointShadowImage;
			layerView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			layerView.format = depthFormat_;
			layerView.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
			if(vkCreateImageView(device, &layerView, nullptr, &f.pointShadowLayerViews[layer]) != VK_SUCCESS){
				return false;
			}
			VkFramebufferCreateInfo layerFb{};
			layerFb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			layerFb.renderPass = shadowRenderPass_;
			layerFb.attachmentCount = 1;
			layerFb.pAttachments = &f.pointShadowLayerViews[layer];
			layerFb.width = kPointShadowSize;
			layerFb.height = kPointShadowSize;
			layerFb.layers = 1;
			if(vkCreateFramebuffer(device, &layerFb, nullptr, &f.pointShadowFramebuffers[layer]) != VK_SUCCESS){
				return false;
			}
		}
		VkImageViewCreateInfo arrayView{};
		arrayView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		arrayView.image = f.pointShadowImage;
		arrayView.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
		arrayView.format = depthFormat_;
		arrayView.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, Frame::kPointShadowLayers};
		if(vkCreateImageView(device, &arrayView, nullptr, &f.pointShadowArrayView) != VK_SUCCESS){
			return false;
		}
		VkDescriptorImageInfo cubeDescriptor{shadowSampler_, f.pointShadowArrayView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
		write.dstBinding = 2;
		write.descriptorCount = 1;
		write.pImageInfo = &cubeDescriptor;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}
	return true;
}

bool VulkanWindow::createShadowPipeline(const char* vertName, VkDescriptorSetLayout setLayout, VkPipelineLayout* layoutOut,
	VkPipeline* out, bool skinned)
{
	VkDevice device = ctx_->device();
	VkShaderModule vert = loadShader((std::string(vertName) + ".vert.spv").c_str());
	if(!vert){
		return false;
	}

	// デプスだけ描く: 頂点シェーダーのみ
	VkPipelineShaderStageCreateInfo stage{};
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	stage.module = vert;
	stage.pName = "main";

	// 入力は位置(location 0)だけ。スキニング付きはボーン番号と重み(location 4, 5)も使う
	VkVertexInputBindingDescription binding = skinned ? vk_::SkinnedVertex::bindingDescription() : vk_::Vertex::bindingDescription();
	std::vector<VkVertexInputAttributeDescription> attributes;
	if(skinned){
		const auto all = vk_::SkinnedVertex::attributeDescriptions();
		attributes = {all[0], all[4], all[5], all[6]};
	}
	else{
		attributes = {vk_::Vertex::attributeDescriptions()[0]};
	}
	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
	vertexInput.pVertexAttributeDescriptions = attributes.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport{};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE; // 十字のような薄い板も影を落とすので、両面とも描く
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	// 深度バイアス: 自分自身の影のシマシマ(shadow acne)を防ぐ
	raster.depthBiasEnable = VK_TRUE;
	raster.depthBiasConstantFactor = 1.25f;
	raster.depthBiasSlopeFactor = 1.75f;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendStateCreateInfo blend{}; // カラーアタッチメント無し
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic{};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;

	// 描画ごとの「光から見たMVP行列」(mat4 = 64B)だけをpush constantで渡す。descriptor setは使わない
	VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16};
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = setLayout ? 1 : 0; // スキニング付きはset 0=ボーン行列
	layoutInfo.pSetLayouts = &setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushRange;
	bool ok = vkCreatePipelineLayout(device, &layoutInfo, nullptr, layoutOut) == VK_SUCCESS;

	if(ok){
		VkGraphicsPipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.stageCount = 1;
		info.pStages = &stage;
		info.pVertexInputState = &vertexInput;
		info.pInputAssemblyState = &inputAssembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &multisample;
		info.pDepthStencilState = &depthStencil;
		info.pColorBlendState = &blend;
		info.pDynamicState = &dynamic;
		info.layout = *layoutOut;
		info.renderPass = shadowRenderPass_;
		info.subpass = 0;
		ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, out) == VK_SUCCESS;
	}
	if(!ok){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create shadow pipeline.");
	}
	vkDestroyShaderModule(device, vert, nullptr);
	return ok;
}

bool VulkanWindow::createDepthResources()
{
	VkDevice device = ctx_->device();
	VkImageCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = depthFormat_;
	info.extent = {renderExtent_.width, renderExtent_.height, 1};
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if(vkCreateImage(device, &info, nullptr, &depthImage_) != VK_SUCCESS){
		return false;
	}

	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, depthImage_, &req);
	const uint32_t type = ctx_->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if(type == UINT32_MAX){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No device-local memory type for depth image.");
		return false;
	}
	VkMemoryAllocateInfo alloc{};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = type;
	if(vkAllocateMemory(device, &alloc, nullptr, &depthMemory_) != VK_SUCCESS
		|| vkBindImageMemory(device, depthImage_, depthMemory_, 0) != VK_SUCCESS){
		return false;
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = depthImage_;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = depthFormat_;
	viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
	return vkCreateImageView(device, &viewInfo, nullptr, &depthView_) == VK_SUCCESS;
}

bool VulkanWindow::createRenderPass()
{
	VkDevice device = ctx_->device();
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(ctx_->physicalDevice(), &props);
		msaaSupported_ = (props.limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0
			&& (props.limits.framebufferDepthSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0;
	}

	// シーンのパス(1xと4x): 結果はsceneColor_(1x)に入り、後処理のパスがフラグメントシェーダーで読む(SHADER_READ_ONLYで終わる)
	const auto makeScenePass = [&](bool msaa, VkRenderPass* out){
		const VkSampleCountFlagBits samples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
		VkAttachmentDescription attachments[3]{};
		// 0: 描画先のカラー(4xなら4xのバッファ。保存せず、リゾルブへ渡すだけ)
		attachments[0].format = format_;
		attachments[0].samples = samples;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// 1: 深度
		attachments[1].format = depthFormat_;
		attachments[1].samples = samples;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 毎フレーム作り直すので保存不要
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		// 2: リゾルブ先(4xのみ)
		attachments[2].format = format_;
		attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		const VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		const VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		const VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;
		subpass.pResolveAttachments = msaa ? &resolveRef : nullptr;

		VkSubpassDependency deps[2]{};
		// 前フレームの後処理がsceneColor_を読み終え、深度の使用が終わるのを待ってから書く
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstSubpass = 0;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
			| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		deps[0].srcAccessMask = 0;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		// このパスの書込み(リゾルブ含む)が終わってから、後処理がフラグメントシェーダーで読む
		deps[1].srcSubpass = 0;
		deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		VkRenderPassCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		info.attachmentCount = msaa ? 3 : 2;
		info.pAttachments = attachments;
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
		info.dependencyCount = 2;
		info.pDependencies = deps;
		if(vkCreateRenderPass(device, &info, nullptr, out) != VK_SUCCESS){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateRenderPass failed.");
			return false;
		}
		return true;
	};
	if(!makeScenePass(false, &renderPass_) || (msaaSupported_ && !makeScenePass(true, &renderPassMsaa_))){
		return false;
	}

	// 後処理のパス: スワップチェーンのイメージへ全面を上書きする(FXAAの有無はシェーダーのpush constantで決める)
	VkAttachmentDescription color{};
	color.format = format_;
	color.samples = VK_SAMPLE_COUNT_1_BIT;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &ref;
	// イメージ取得(imageAvailable)を待ってからカラー出力する
	VkSubpassDependency dep{};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.srcAccessMask = 0;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	VkRenderPassCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = 1;
	info.pAttachments = &color;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 1;
	info.pDependencies = &dep;
	if(vkCreateRenderPass(device, &info, nullptr, &postRenderPass_) != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateRenderPass (post) failed.");
		return false;
	}
	return true;
}

bool VulkanWindow::createAttachment(VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
	VkImageAspectFlags aspect, Attachment& out)
{
	VkDevice device = ctx_->device();
	VkImageCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent = {renderExtent_.width, renderExtent_.height, 1};
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = samples;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if(vkCreateImage(device, &info, nullptr, &out.image) != VK_SUCCESS){
		return false;
	}
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, out.image, &req);
	const uint32_t type = ctx_->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if(type == UINT32_MAX){
		return false;
	}
	VkMemoryAllocateInfo alloc{};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = type;
	if(vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS
		|| vkBindImageMemory(device, out.image, out.memory, 0) != VK_SUCCESS){
		return false;
	}
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = out.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
	return vkCreateImageView(device, &viewInfo, nullptr, &out.view) == VK_SUCCESS;
}

void VulkanWindow::destroyAttachment(Attachment& a)
{
	VkDevice device = ctx_->device();
	if(a.view){ vkDestroyImageView(device, a.view, nullptr); }
	if(a.image){ vkDestroyImage(device, a.image, nullptr); }
	if(a.memory){ vkFreeMemory(device, a.memory, nullptr); }
	a = {};
}

// シーンの描画先(スワップチェーンと同じサイズ。リサイズ時に作り直す): 1xのカラー、4x用のカラー・深度、フレームバッファ、後処理が読むdescriptor set
bool VulkanWindow::createSceneTargets()
{
	VkDevice device = ctx_->device();
	if(!createAttachment(format_, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, sceneColor_)){
		return false;
	}
	VkFramebufferCreateInfo fbInfo{};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.width = renderExtent_.width;
	fbInfo.height = renderExtent_.height;
	fbInfo.layers = 1;
	{
		const VkImageView attachments[] = {sceneColor_.view, depthView_};
		fbInfo.renderPass = renderPass_;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = attachments;
		if(vkCreateFramebuffer(device, &fbInfo, nullptr, &sceneFramebuffer_) != VK_SUCCESS){
			return false;
		}
	}
	if(msaaSupported_){
		if(!createAttachment(format_, VK_SAMPLE_COUNT_4_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, msaaColor_)
			|| !createAttachment(depthFormat_, VK_SAMPLE_COUNT_4_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, msaaDepth_)){
			return false;
		}
		const VkImageView attachments[] = {msaaColor_.view, msaaDepth_.view, sceneColor_.view};
		fbInfo.renderPass = renderPassMsaa_;
		fbInfo.attachmentCount = 3;
		fbInfo.pAttachments = attachments;
		if(vkCreateFramebuffer(device, &fbInfo, nullptr, &sceneMsaaFramebuffer_) != VK_SUCCESS){
			return false;
		}
	}
	// 後処理のdescriptor set: sceneColor_を読む(作り直しの前にvkDeviceWaitIdle済みなので、書き換えてよい)
	VkDescriptorImageInfo imageInfo{postSampler_, sceneColor_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = postSet_;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	return true;
}

void VulkanWindow::destroySceneTargets()
{
	VkDevice device = ctx_->device();
	if(sceneFramebuffer_){ vkDestroyFramebuffer(device, sceneFramebuffer_, nullptr); sceneFramebuffer_ = VK_NULL_HANDLE; }
	if(sceneMsaaFramebuffer_){ vkDestroyFramebuffer(device, sceneMsaaFramebuffer_, nullptr); sceneMsaaFramebuffer_ = VK_NULL_HANDLE; }
	destroyAttachment(sceneColor_);
	destroyAttachment(msaaColor_);
	destroyAttachment(msaaDepth_);
}

// 後処理(全画面の三角形でsceneColor_を読み、必要ならFXAAを掛けてスワップチェーンへ出す)の、一度だけ作るもの
bool VulkanWindow::createPostResources()
{
	VkDevice device = ctx_->device();
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if(vkCreateSampler(device, &samplerInfo, nullptr, &postSampler_) != VK_SUCCESS){
		return false;
	}
	VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;
	if(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &postSetLayout_) != VK_SUCCESS){
		return false;
	}
	const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &postSetLayout_;
	if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &postPool_) != VK_SUCCESS){
		return false;
	}
	allocInfo.descriptorPool = postPool_;
	if(vkAllocateDescriptorSets(device, &allocInfo, &postSet_) != VK_SUCCESS){
		return false;
	}
	// push constant(fragment): [0]=(シーンの1テクセルの大きさ(uv), FXAAを掛けるか)、[1]=スワップチェーンの1ピクセルの大きさ(uv)
	const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 8};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &postSetLayout_;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &push;
	if(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &postLayout_) != VK_SUCCESS){
		return false;
	}

	VkShaderModule vert = loadShader("post.vert.spv");
	VkShaderModule frag = loadShader("post.frag.spv");
	if(!vert || !frag){
		if(vert){ vkDestroyShaderModule(device, vert, nullptr); }
		if(frag){ vkDestroyShaderModule(device, frag, nullptr); }
		return false;
	}
	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";
	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport{};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample{};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blend{};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAttachment;
	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic{};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewport;
	pipelineInfo.pRasterizationState = &raster;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pColorBlendState = &blend;
	pipelineInfo.pDynamicState = &dynamic;
	pipelineInfo.layout = postLayout_;
	pipelineInfo.renderPass = postRenderPass_;
	pipelineInfo.subpass = 0;
	const bool ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &postPipeline_) == VK_SUCCESS;
	vkDestroyShaderModule(device, vert, nullptr);
	vkDestroyShaderModule(device, frag, nullptr);
	return ok;
}

VkShaderModule VulkanWindow::loadShader(const char* name)
{
	const char* base = SDL_GetBasePath();
	const std::string path = std::string(base ? base : "") + "shaders/" + name;
	size_t size = 0;
	void* code = SDL_LoadFile(path.c_str(), &size);
	if(!code){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load shader %s. %s", path.c_str(), SDL_GetError());
		return VK_NULL_HANDLE;
	}
	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = size;
	info.pCode = static_cast<const uint32_t*>(code); // SDL_LoadFileの戻りはアライン済み
	VkShaderModule module = VK_NULL_HANDLE;
	if(vkCreateShaderModule(ctx_->device(), &info, nullptr, &module) != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateShaderModule failed. %s", path.c_str());
	}
	SDL_free(code);
	return module;
}

bool VulkanWindow::createPipelines()
{
	VkDevice device = ctx_->device();
	const VkDescriptorSetLayout setLayouts[] = {descriptorSetLayout_, texturePool_->layout()};

	// ライティング付き: オブジェクトごとのMVP行列とモデル行列(mat4 x2 = 128B。仕様の保証最小値)をpush constantで渡す
	VkPushConstantRange litPush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 32};
	// スプライト: MVP行列・色・テクスチャの範囲(mat4 + vec4 x2 = 96B)
	VkPushConstantRange spritePush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 24};
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 2;
	layoutInfo.pSetLayouts = setLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &litPush;
	if(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &litLayout_) != VK_SUCCESS){
		return false;
	}
	layoutInfo.pPushConstantRanges = &spritePush;
	if(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &spriteLayout_) != VK_SUCCESS){
		return false;
	}

	// スキニング付き: 同じ配置にset 2(ボーン行列)を足す
	const VkDescriptorSetLayout skinSetLayouts[] = {descriptorSetLayout_, texturePool_->layout(), bonePool_->layout()};
	layoutInfo.setLayoutCount = 3;
	layoutInfo.pSetLayouts = skinSetLayouts;
	layoutInfo.pPushConstantRanges = &litPush;
	if(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &litSkinLayout_) != VK_SUCCESS){
		return false;
	}

	// 輪郭線: push 80B(mvp + vec4)、スキニング版はset 0=ボーン行列
	VkPushConstantRange outlinePush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 24};
	VkPipelineLayoutCreateInfo outlineInfo{};
	outlineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	outlineInfo.pushConstantRangeCount = 1;
	outlineInfo.pPushConstantRanges = &outlinePush;
	// set 1 = テクスチャ(アルファの切り抜きの判定用)。通常版のset 0は使わない(フレームUBOの配置をそろえるだけ)、スキニング版のset 0はボーン行列
	const VkDescriptorSetLayout staticOutlineSets[] = {descriptorSetLayout_, texturePool_->layout()};
	outlineInfo.setLayoutCount = 2;
	outlineInfo.pSetLayouts = staticOutlineSets;
	if(vkCreatePipelineLayout(device, &outlineInfo, nullptr, &outlineLayout_) != VK_SUCCESS){
		return false;
	}
	const VkDescriptorSetLayout skinOutlineSets[] = {bonePool_->layout(), texturePool_->layout()};
	outlineInfo.pSetLayouts = skinOutlineSets;
	if(vkCreatePipelineLayout(device, &outlineInfo, nullptr, &outlineSkinLayout_) != VK_SUCCESS){
		return false;
	}

	// シーンのパイプラインを1xと4xの両方で作る(4xに対応しないGPUでは1xだけ)
	for(int variant = 0; variant < (msaaSupported_ ? 2 : 1); ++variant){
		const bool msaa = variant == 1;
		auto& p = scenePipelines_[static_cast<size_t>(variant)];
		if(!(createGraphicsPipeline("triangle", "triangle", litLayout_, true, true, true, &p.lit, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("outline", "outline", outlineLayout_, true, true, false, &p.outline, false, VK_CULL_MODE_FRONT_BIT, msaa)
			&& createGraphicsPipeline("outline_pre", "outline", outlineSkinLayout_, true, true, false, &p.outlineSkin, false, VK_CULL_MODE_FRONT_BIT, msaa)
			&& createGraphicsPipeline("triangle", "triangle_opaque", litLayout_, true, true, false, &p.litOpaque, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("triangle_pre", "triangle_opaque", litSkinLayout_, true, true, false, &p.litSkinOpaque, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("triangle", "triangle_mask", litLayout_, true, true, false, &p.litMask, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("triangle_pre", "triangle_mask", litSkinLayout_, true, true, false, &p.litSkinMask, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("outline", "outline_opaque", outlineLayout_, true, true, false, &p.outlineOpaque, false, VK_CULL_MODE_FRONT_BIT, msaa)
			&& createGraphicsPipeline("outline_pre", "outline_opaque", outlineSkinLayout_, true, true, false, &p.outlineSkinOpaque, false, VK_CULL_MODE_FRONT_BIT, msaa)
			&& createGraphicsPipeline("triangle_pre", "triangle", litSkinLayout_, true, true, true, &p.litSkin, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("sprite", "sprite", spriteLayout_, false, false, true, &p.sprite2d, false, VK_CULL_MODE_NONE, msaa)
			&& createGraphicsPipeline("sprite", "sprite", spriteLayout_, true, false, true, &p.sprite3d, false, VK_CULL_MODE_NONE, msaa))){
			return false;
		}
	}
	return createPostResources()
		&& createShadowPipeline("shadow", VK_NULL_HANDLE, &shadowLayout_, &shadowPipeline_, false)
		&& createShadowPipeline("shadow_pre", bonePool_->layout(), &shadowSkinLayout_, &shadowSkinPipeline_, false)
		&& createSkinPipeline();
}

// スキニングのコンピュートパイプライン(res/shaders/skin.comp)
bool VulkanWindow::createSkinPipeline()
{
	VkDevice device = ctx_->device();
	const VkDescriptorSetLayout setLayouts[] = {bonePool_->layout(), bonePool_->partLayout()};
	const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)};
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 2;
	layoutInfo.pSetLayouts = setLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &push;
	if(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &skinLayout_) != VK_SUCCESS){
		return false;
	}
	VkShaderModule module = loadShader("skin.comp.spv");
	if(!module){
		return false;
	}
	VkComputePipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.stage.module = module;
	info.stage.pName = "main";
	info.layout = skinLayout_;
	const bool ok = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &skinPipeline_) == VK_SUCCESS;
	vkDestroyShaderModule(device, module, nullptr);
	if(!ok){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create skinning compute pipeline.");
	}
	return ok;
}

bool VulkanWindow::createGraphicsPipeline(const char* vertName, const char* fragName, VkPipelineLayout layout,
	bool depthTest, bool depthWrite, bool blendEnable, VkPipeline* out, bool skinned, VkCullModeFlags cullMode, bool msaa)
{
	VkDevice device = ctx_->device();
	VkShaderModule vert = loadShader((std::string(vertName) + ".vert.spv").c_str());
	VkShaderModule frag = loadShader((std::string(fragName) + ".frag.spv").c_str());
	if(!vert || !frag){
		if(vert){ vkDestroyShaderModule(device, vert, nullptr); }
		if(frag){ vkDestroyShaderModule(device, frag, nullptr); }
		return false;
	}

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";

	const VkVertexInputBindingDescription binding = skinned ? vk_::SkinnedVertex::bindingDescription() : vk_::Vertex::bindingDescription();
	std::vector<VkVertexInputAttributeDescription> attributes;
	if(skinned){
		const auto all = vk_::SkinnedVertex::attributeDescriptions();
		attributes.assign(all.begin(), all.end());
	}
	else{
		const auto all = vk_::Vertex::attributeDescriptions();
		attributes.assign(all.begin(), all.end());
	}
	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
	vertexInput.pVertexAttributeDescriptions = attributes.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport{};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = cullMode;
	// 反時計回り(視覚的に)が表面。メッシュは外向きが反時計回りの頂点順。
	// Y反転はプロジェクション側(vk_::createPerspective/createOrtho)で行っているため、通常の規約と一致する
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; // 手前(小さいz)が勝つ。クリア値は1.0

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if(blendEnable){
		// 通常のアルファブレンド: 出力 = src * a + dst * (1 - a)
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	VkPipelineColorBlendStateCreateInfo blend{};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAttachment;

	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic{};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;

	bool ok = true;

	if(ok){
		VkGraphicsPipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.stageCount = 2;
		info.pStages = stages;
		info.pVertexInputState = &vertexInput;
		info.pInputAssemblyState = &inputAssembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &multisample;
		info.pDepthStencilState = &depthStencil;
		info.pColorBlendState = &blend;
		info.pDynamicState = &dynamic;
		info.layout = layout;
		info.renderPass = msaa ? renderPassMsaa_ : renderPass_;
		info.subpass = 0;
		ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, out) == VK_SUCCESS;
	}
	if(!ok){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create graphics pipeline.");
	}
	vkDestroyShaderModule(device, vert, nullptr);
	vkDestroyShaderModule(device, frag, nullptr);
	return ok;
}

namespace
{
// AABB(mn,mx)の8隅を行列m(列優先のmvp)でクリップ空間へ送り、1つの平面の外に全隅があれば視錐台の外。
// Vulkanのクリップ空間: -w<=x<=w, -w<=y<=w, 0<=z<=w(vk_::createPerspective/createOrthoと同じ)
bool aabbOutsideFrustum(const float* m, const float* mn, const float* mx)
{
	unsigned outsideAll = 0x3F;
	for(int corner = 0; corner < 8; ++corner){
		const float x = (corner & 1) ? mx[0] : mn[0];
		const float y = (corner & 2) ? mx[1] : mn[1];
		const float z = (corner & 4) ? mx[2] : mn[2];
		const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
		const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
		const float cz = m[2] * x + m[6] * y + m[10] * z + m[14];
		const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
		unsigned mask = 0;
		mask |= cx < -cw ? 1u : 0u;
		mask |= cx > cw ? 2u : 0u;
		mask |= cy < -cw ? 4u : 0u;
		mask |= cy > cw ? 8u : 0u;
		mask |= cz < 0.0f ? 16u : 0u;
		mask |= cz > cw ? 32u : 0u;
		outsideAll &= mask;
		if(outsideAll == 0){
			return false;
		}
	}
	return true;
}
}

void VulkanWindow::swap()
{
	// 予約はこのswap()で消費する(描画できない場合も溜め込まない)
	auto commands = std::move(pending_);
	pending_.clear();
	geo::Matrix4x4f lightViewProj = geo::createIdentityMatrix4x4<float>();
	if(!ready_){
		return;
	}
	VkDevice device = ctx_->device();

	if(swapchainDirty_ || !swapchain_){
		vkDeviceWaitIdle(device);
		destroySwapchain();
		swapchainDirty_ = false;
		if(!createSwapchain()){
			ready_ = false;
			return;
		}
		if(!swapchain_){
			return; // 最小化中
		}
	}

	// このフレーム枠を前回使ったGPU処理の完了を待つ(他の枠はGPU処理中でもよい)
	Frame& frame = frames_[frameIndex_];
	const auto toMs = [](uint64_t a, uint64_t b){ return static_cast<double>(b - a) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()); };
	const uint64_t tWait0 = SDL_GetPerformanceCounter();
	vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
	const uint64_t tWait1 = SDL_GetPerformanceCounter();

	// 前回この枠で記録したGPUタイムスタンプを回収する(フェンス完了済み)
	if(profile_ && frame.queryPending){
		uint64_t ts[kTimestampCount];
		if(vkGetQueryPoolResults(device, frame.queryPool, 0, kTimestampCount, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS){
			for(int i = 0; i < kTimestampCount - 1; ++i){
				stats_.gpuMs[i] += static_cast<double>((ts[i + 1] - ts[i]) & timestampMask_) * timestampPeriod_ * 1e-6;
			}
			++stats_.gpuFrames;
		}
		frame.queryPending = false;
	}

	// この枠の前回のGPU処理は完了済みなので、前回のメッシュ参照を手放してよい
	frame.meshes.clear();
	frame.textures.clear();
	frame.models.clear();

	// 予約のAABB(カリング・ソート用)。メッシュ空間の範囲で、ライティング付きの予約だけ持つ
	// (スキニングのモデルは、現在の姿勢のモデル全体。VulkanModel::updatePose()で更新される)
	const auto boundsOf = [](const DrawCommand& c, const float*& mn, const float*& mx){
		if(c.kind == DrawKind::LitSkinned){
			if(c.owner && c.owner->hasPoseBounds()){
				mn = c.owner->poseBoundsMin();
				mx = c.owner->poseBoundsMax();
				return true;
			}
		}
		else if(c.kind == DrawKind::Lit && c.mesh){
			mn = c.mesh->boundsMin();
			mx = c.mesh->boundsMax();
			return true;
		}
		return false;
	};

	// 描く頂点バッファ: スキニングのモデルは、コンピュートで書き出したスキニング済みの頂点(この枠のもの)。それ以外はメッシュの頂点バッファ
	const auto vertexBufferOf = [this](const DrawCommand& c){
		if(c.kind == DrawKind::LitSkinned && c.owner){
			return c.owner->skinnedVertexBuffer(c.partIndex, static_cast<int>(frameIndex_));
		}
		return c.mesh->vertexBuffer();
	};

	// 描くときの vertexOffset: スキニングのモデルは、スキニング済みの頂点バッファの中での部品の先頭(インデックスは部品ごとに0始まり)
	const auto vertexOffsetOf = [](const DrawCommand& c){
		return c.kind == DrawKind::LitSkinned && c.owner ? c.owner->skinnedVertexOffset(c.partIndex) : 0;
	};

	// 描画順: 不透明(予約順) → 半透明(モデル単位で、カメラから遠いものから) → スプライト(予約順)。
	// 半透明 = 不透明度が1未満の材質か、半透明のピクセルを持つテクスチャ。同じモデルの部品は、元ファイルの並び順を保つ
	// (顔・目・まつげなど、同一面の重なりを順番で解決しているため)
	{
		struct Item
		{
			DrawCommand command;
			const void* key;
			float distance;
		};
		std::vector<DrawCommand> opaque, other;
		std::vector<Item> translucent;
		const float eye[3] = {uniform_.cameraPos[0], uniform_.cameraPos[1], uniform_.cameraPos[2]};
		for(auto& c : commands){
			const bool lit = c.kind == DrawKind::Lit || c.kind == DrawKind::LitSkinned;
			if(!lit){
				other.push_back(std::move(c));
			}
			else if((static_cast<int>(c.params[2] + 0.5f) >> 16) == 0 && (c.params[3] < 0.999f || (c.texture && c.texture->hasAlpha()))){
				// モデル行列でワールドへ送ったAABBの中心から、カメラまでの距離
				float distance = 0.0f;
				const float *mn = nullptr, *mx = nullptr;
				if(boundsOf(c, mn, mx)){
					const float local[3] = {(mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f};
					const float* m = c.model.data();
					for(int row = 0; row < 3; ++row){
						const float world = m[row] * local[0] + m[4 + row] * local[1] + m[8 + row] * local[2] + m[12 + row];
						distance += (world - eye[row]) * (world - eye[row]);
					}
				}
				const void* key = c.owner ? static_cast<const void*>(c.owner.get()) : static_cast<const void*>(c.mesh.get());
				translucent.push_back({std::move(c), key, distance});
			}
			else{
				opaque.push_back(std::move(c));
			}
		}
		// グループ(モデル)の距離は、最初の部品のもの。stable_sortなので、同じグループの部品の並びは保たれる
		std::vector<std::pair<const void*, float>> groups;
		for(const auto& item : translucent){
			if(std::none_of(groups.begin(), groups.end(), [&](const auto& g){ return g.first == item.key; })){
				groups.emplace_back(item.key, item.distance);
			}
		}
		const auto groupDistance = [&](const void* key){
			for(const auto& g : groups){
				if(g.first == key){
					return g.second;
				}
			}
			return 0.0f;
		};
		std::stable_sort(translucent.begin(), translucent.end(), [&](const Item& a, const Item& b){
			return groupDistance(a.key) > groupDistance(b.key);
		});
		commands = std::move(opaque);
		for(auto& item : translucent){
			commands.push_back(std::move(item.command));
		}
		for(auto& c : other){
			commands.push_back(std::move(c));
		}
	}
	// フラスタムカリング(メインのパス)。1なら視錐台の外
	std::vector<char> culled(commands.size(), 0);
	int culledCount = 0;
	if(frustumCulling_){
		for(size_t i = 0; i < commands.size(); ++i){
			const float *mn = nullptr, *mx = nullptr;
			if(boundsOf(commands[i], mn, mx) && aabbOutsideFrustum(commands[i].mvp.data(), mn, mx)){
				culled[i] = 1;
				++culledCount;
			}
		}
	}
	int shadowCulled = 0; // 影のパスで省いた数(全パスの合計)

	// モデルのスキニング: この枠のボーン行列を現在の姿勢に合わせ、描画に使うdescriptor setを決める
	// (この枠の前回のGPU処理はフェンスで完了済みなので、ボーン行列のバッファを書き換えてよい)
	{
		std::unordered_set<VulkanModel*> prepared;
		for(auto& command : commands){
			if(command.owner){
				if(prepared.insert(command.owner.get()).second){
					command.owner->prepareSlot(static_cast<int>(frameIndex_));
					frame.models.push_back(command.owner); // GPUが使い終わるまで(この枠のfence完了まで)生かしておく
				}
				command.boneSet = command.owner->boneSet(static_cast<int>(frameIndex_));
			}
		}
	}

	const uint64_t tPrep1 = SDL_GetPerformanceCounter();

	// 平行光源から見たビュー射影(影用)。シャドウマップの範囲(球)がすっぽり入る平行投影で、光は球の中心を向く
	{
		const float radius = shadowRadius_;
		const geo::Vector3f dir(uniform_.lightDir[0], uniform_.lightDir[1], uniform_.lightDir[2]);
		const geo::Vector3f eye = shadowCenter_ - dir * radius;
		const geo::Vector3f up = std::abs(dir.getY()) > 0.99f ? geo::Vector3f(1.0f, 0.0f, 0.0f) : geo::Vector3f(0.0f, 1.0f, 0.0f);
		const auto lightView = geo::createLookAt<float>(eye, shadowCenter_, up);
		const auto lightProj = vk_::createOrtho(radius * 2.0f, radius * 2.0f, 0.1f, radius * 2.0f + 1.0f);
		lightViewProj = lightProj * lightView;
		std::copy(lightViewProj.data(), lightViewProj.data() + 16, uniform_.lightViewProj.begin());
		uniform_.shadowParams[3] = radius * 2.0f / static_cast<float>(kShadowMapSize); // 平行光源の1テクセルの大きさ(ワールド)
	}

	// 有効な点光源の数(色が黒でない最後の番号+1)。シェーダーはこの数だけ光源を処理する
	{
		int count = 0;
		for(int i = 0; i < kMaxPointLights; ++i){
			const auto& c = uniform_.pointColor[i];
			if(c[0] + c[1] + c[2] > 0.0f){
				count = i + 1;
			}
		}
		uniform_.pointShadow[2] = static_cast<float>(count);
	}

	// uniform buffer更新(lightDir.wをシェーダーへの「シャドウマップを参照するか」の印にする)
	uniform_.lightDir[3] = shadowMapsEnabled_ ? 1.0f : 0.0f;
	frame.uniformBuffer->write(&uniform_, sizeof(UniformData));

	uint32_t index = 0;
	const uint64_t tAcq0 = SDL_GetPerformanceCounter();
	VkResult r = vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &index);
	const uint64_t tAcq1 = SDL_GetPerformanceCounter();
	if(r == VK_ERROR_OUT_OF_DATE_KHR){
		swapchainDirty_ = true;
		return;
	}
	if(r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkAcquireNextImageKHR failed. VkResult=%d", static_cast<int>(r));
		return;
	}
	vkResetFences(device, 1, &frame.inFlight);
	VkCommandBuffer cmd = frame.commandBuffer;

	// 記録: レンダーパス(クリア)内で予約されたメッシュを描画。PRESENT_SRCへの遷移もレンダーパスが行う
	vkResetCommandBuffer(cmd, 0);
	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin);
	const uint64_t tRec0 = SDL_GetPerformanceCounter();
	// GPUタイムスタンプ: 区間の終わりに書く(BOTTOM_OF_PIPEなのでそこまでの処理がすべて終わった時刻)
	const auto stamp = [&](TimestampPoint point){
		if(profile_){
			vkCmdWriteTimestamp(cmd, point == kTsStart ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, point);
		}
	};
	if(profile_){
		vkCmdResetQueryPool(cmd, frame.queryPool, 0, kTimestampCount);
	}
	stamp(kTsStart);

	// スキニング: ボーン行列かモーフが変わったモデルを、部品ごとにコンピュートで1回だけスキニングして、スキニング済みの頂点を書き出す。
	// 影・本体・輪郭線の各パスは、これを普通の頂点として読む(同じスキニングを何度も計算しない)
	{
		// このフレームで結果が要るモデルだけスキニングする: メインのパスで描くもの、または影を落とす物(平行光の視錐台に入る、
		// または影を落とす点光源がある場合は、念のため)。要らないモデルは、ボーン行列が古いまま印を残し、要るときに更新する
		bool anyPointShadow = false;
		for(int i = 0; i < kMaxPointLights; ++i){
			const auto& c = uniform_.pointColor[i];
			anyPointShadow = anyPointShadow || (c[0] + c[1] + c[2] > 0.0f && uniform_.pointAtten[i][3] > 0.5f);
		}
		std::unordered_set<const VulkanModel*> neededModels;
		for(size_t ci = 0; ci < commands.size(); ++ci){
			const auto& command = commands[ci];
			if(command.kind != DrawKind::LitSkinned || !command.owner || neededModels.count(command.owner.get())){
				continue;
			}
			bool needed = !frustumCulling_ || !culled[ci];
			if(!needed && command.castShadow && shadowMapsEnabled_){
				const float *mn = nullptr, *mx = nullptr;
				needed = anyPointShadow || !boundsOf(command, mn, mx) || !aabbOutsideFrustum((lightViewProj * command.model).data(), mn, mx);
			}
			if(needed){
				neededModels.insert(command.owner.get());
			}
		}
		bool bound = false;
		for(const auto& owner : frame.models){
			if(!owner->skeleton() || !neededModels.count(owner.get()) || !owner->takeSkinDirty(static_cast<int>(frameIndex_))){
				continue;
			}
			if(!bound){
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinPipeline_);
				bound = true;
			}
			const VkDescriptorSet boneSet = owner->boneSet(static_cast<int>(frameIndex_));
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinLayout_, 0, 1, &boneSet, 0, nullptr);
			for(size_t g = 0; g < owner->skinGroupCount(); ++g){
				const uint32_t count = owner->skinGroupVertexCount(g);
				const VkDescriptorSet groupSet = owner->skinGroupSet(g, static_cast<int>(frameIndex_));
				if(!groupSet || count == 0){
					continue;
				}
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinLayout_, 1, 1, &groupSet, 0, nullptr);
				vkCmdPushConstants(cmd, skinLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(count), &count);
				// 1次元のワークグループ数の上限(maxComputeWorkGroupCount[0])を超える頂点数は、2次元に折り返す(シェーダーが番号を復元する)
				const uint32_t groups = (count + 63) / 64;
				const uint32_t groupsX = std::min(groups, skinMaxGroupsX_);
				vkCmdDispatch(cmd, groupsX, (groups + groupsX - 1) / groupsX, 1);
			}
		}
		if(bound){
			// コンピュートの書込みが終わってから、頂点の入力として読む
			VkMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}
	}
	stamp(kTsSkin);

	// 影用の描画(デプスのみ)を、視点のビュー射影viewProjで記録する。通常の頂点とスキニング付きで、パイプラインを切り替える
	const auto recordShadowCommands = [&](const geo::Matrix4x4f& viewProj, const std::vector<char>* casterMask){
		if(!shadowMapsEnabled_){
			return; // シャドウマップ無効: 何も描かない(最初の1回だけ、クリアのためにパスを開始する)
		}
		bool skinnedBound = false;
		bool anyBound = false;
		for(size_t ci = 0; ci < commands.size(); ++ci){
			const auto& command = commands[ci];
			if((command.kind != DrawKind::Lit && command.kind != DrawKind::LitSkinned) || !command.castShadow
				|| (casterMask && !(*casterMask)[ci])){
				continue;
			}
			const bool skinned = command.kind == DrawKind::LitSkinned;
			if(skinned && !command.boneSet){
				continue;
			}
			const auto lightMvp = viewProj * command.model;
			{
				const float *mn = nullptr, *mx = nullptr;
				if(frustumCulling_ && boundsOf(command, mn, mx) && aabbOutsideFrustum(lightMvp.data(), mn, mx)){
					++shadowCulled; // この光の視錐台の外: 影は落ちない
					continue;
				}
			}
			if(!anyBound || skinned != skinnedBound){
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned ? shadowSkinPipeline_ : shadowPipeline_);
				skinnedBound = skinned;
				anyBound = true;
			}
			if(skinned){
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSkinLayout_, 0, 1, &command.boneSet, 0, nullptr);
			}
			const VkBuffer vb = vertexBufferOf(command);
			const VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
			vkCmdBindIndexBuffer(cmd, command.mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(cmd, skinned ? shadowSkinLayout_ : shadowLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, lightMvp.data());
			vkCmdDrawIndexed(cmd, command.mesh->indexCount(), 1, 0, vertexOffsetOf(command), command.materialIndex);
		}
	};

	// 1. シャドウマップ: 影を落とすメッシュを、光から見たデプスだけで描く(無効のときは、一度クリアしたら以降は省く)
	if(shadowMapsEnabled_ || !frame.dirShadowValid){
		frame.dirShadowValid = true;
		VkClearValue shadowClear{};
		shadowClear.depthStencil = {1.0f, 0};
		VkRenderPassBeginInfo shadowBegin{};
		shadowBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		shadowBegin.renderPass = shadowRenderPass_;
		shadowBegin.framebuffer = frame.shadowFramebuffer;
		shadowBegin.renderArea.extent = {kShadowMapSize, kShadowMapSize};
		shadowBegin.clearValueCount = 1;
		shadowBegin.pClearValues = &shadowClear;
		vkCmdBeginRenderPass(cmd, &shadowBegin, VK_SUBPASS_CONTENTS_INLINE);
		const VkViewport shadowViewport{0.0f, 0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.0f, 1.0f};
		const VkRect2D shadowScissor{{0, 0}, {kShadowMapSize, kShadowMapSize}};
		vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
		vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
		recordShadowCommands(lightViewProj, nullptr);
		vkCmdEndRenderPass(cmd);
	}
	stamp(kTsDirShadow);

	// 1b. 点光源ごとのシャドウマップ(キューブマップの6面)。光源位置から各軸方向へ90度の視野で、同じデプス専用パイプラインを使う。
	//     光源が無効でも、メインのパスが参照できるレイアウトにするため毎フレーム全光源の6面とも描く(影を落とすものが無ければクリアだけ)
	{
		const float nearZ = uniform_.pointShadow[1];
		const float farZ = uniform_.pointShadow[0];
		VkClearValue shadowClear{};
		shadowClear.depthStencil = {1.0f, 0};
		const VkViewport viewport{0.0f, 0.0f, static_cast<float>(kPointShadowSize), static_cast<float>(kPointShadowSize), 0.0f, 1.0f};
		const VkRect2D scissor{{0, 0}, {kPointShadowSize, kPointShadowSize}};
		for(int light = 0; light < kMaxPointLights; ++light){
			const auto& pos = uniform_.pointPos[light];
			const geo::Vector3f lightEye(pos[0], pos[1], pos[2]);
			// 更新の間引き: この枠のキューブマップは、interval回に1回(光源ごとにずらして)だけ描き直す。描かない回は前回の内容(枠ごとに持つ)を使う。
			// まだ一度も描いていない光源は必ず描く。描かない光源は、レンダーパスを始めないので、画像は読み取り専用のレイアウトのまま残る
			const bool refresh = !frame.pointShadowValid[light] || (shadowMapsEnabled_ && (pointShadowInterval_ <= 1
				|| (frame.pointShadowUses + static_cast<uint32_t>(light)) % static_cast<uint32_t>(pointShadowInterval_) == 0));
			if(!refresh){
				continue;
			}
			frame.pointShadowValid[light] = true;
			// この光源の影を落とす物の絞り込み: 影を落とさない光源・消えている光源は無し。
			// 光の強さが pointShadowCutoff_(最大の色成分で見た明るさ)を下回る距離より遠い物は、その影も見えないので描かない
			// (距離の減衰が 1/(c + l*d + q*d^2)。影は光源から見て物の外側にしか落ちないので、範囲の外の物は範囲の中へ影を落とさない)
			std::vector<char> casterMask;
			const std::vector<char>* maskPtr = nullptr;
			{
				const auto& color = uniform_.pointColor[light];
				const auto& atten = uniform_.pointAtten[light];
				const float intensity = std::max({color[0], color[1], color[2]});
				const bool active = intensity > 0.0f && atten[3] > 0.5f;
				if(!active || pointShadowCutoff_ > 0.0f){
					float radius = farZ;
					if(active){
						const float k = intensity / pointShadowCutoff_ - atten[0]; // l*d + q*d^2 = k
						if(k > 0.0f){
							radius = atten[2] > 1e-6f ? (-atten[1] + std::sqrt(atten[1] * atten[1] + 4.0f * atten[2] * k)) / (2.0f * atten[2])
								: (atten[1] > 1e-6f ? k / atten[1] : farZ);
						}
						radius = std::min(radius, farZ);
					}
					casterMask.assign(commands.size(), 0);
					for(size_t ci = 0; active && ci < commands.size(); ++ci){
						const float *mn = nullptr, *mx = nullptr;
						if(!boundsOf(commands[ci], mn, mx)){
							casterMask[ci] = 1; // 範囲が分からないものは描く
							continue;
						}
						// ワールド空間のAABB(8隅をモデル行列で送る)と光源との最短距離
						const float* m = commands[ci].model.data();
						float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
						for(int corner = 0; corner < 8; ++corner){
							const float x = (corner & 1) ? mx[0] : mn[0];
							const float y = (corner & 2) ? mx[1] : mn[1];
							const float z = (corner & 4) ? mx[2] : mn[2];
							for(int row = 0; row < 3; ++row){
								const float w = m[row] * x + m[4 + row] * y + m[8 + row] * z + m[12 + row];
								lo[row] = std::min(lo[row], w);
								hi[row] = std::max(hi[row], w);
							}
						}
						float distSq = 0.0f;
						for(int k = 0; k < 3; ++k){
							const float d = std::max({lo[k] - pos[k], 0.0f, pos[k] - hi[k]});
							distSq += d * d;
						}
						casterMask[ci] = distSq <= radius * radius ? 1 : 0;
					}
					maskPtr = &casterMask;
				}
			}
			for(int face = 0; face < 6; ++face){
				const auto faceViewProj = vk_::createCubeFaceViewProj(lightEye, face, nearZ, farZ);
				VkRenderPassBeginInfo faceBegin{};
				faceBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				faceBegin.renderPass = shadowRenderPass_;
				faceBegin.framebuffer = frame.pointShadowFramebuffers[light * 6 + face]; // 配列の層 = 光源の番号*6 + 面
				faceBegin.renderArea.extent = {kPointShadowSize, kPointShadowSize};
				faceBegin.clearValueCount = 1;
				faceBegin.pClearValues = &shadowClear;
				vkCmdBeginRenderPass(cmd, &faceBegin, VK_SUBPASS_CONTENTS_INLINE);
				vkCmdSetViewport(cmd, 0, 1, &viewport);
				vkCmdSetScissor(cmd, 0, 1, &scissor);
				recordShadowCommands(faceViewProj, maskPtr);
				vkCmdEndRenderPass(cmd);
			}
		}
	}

	++frame.pointShadowUses;
	stamp(kTsPointShadow);

	// 2. メインのパス: シーンをオフスクリーン(sceneColor_)へ描く。MSAA有効なら4xで描いて、パスの終わりにsceneColor_へリゾルブする
	const bool useMsaa = msaaSupported_ && (antiAliasing_ == AntiAliasing::Msaa4x || antiAliasing_ == AntiAliasing::Msaa4xFxaa);
	const ScenePipelines& pipes = scenePipelines_[useMsaa ? 1 : 0];
	VkClearValue clears[2]{};
	clears[0].color = clearColor_;
	clears[1].depthStencil = {1.0f, 0};
	VkRenderPassBeginInfo rpBegin{};
	rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBegin.renderPass = useMsaa ? renderPassMsaa_ : renderPass_;
	rpBegin.framebuffer = useMsaa ? sceneMsaaFramebuffer_ : sceneFramebuffer_;
	rpBegin.renderArea.extent = renderExtent_;
	rpBegin.clearValueCount = 2;
	rpBegin.pClearValues = clears;
	vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport vp{0.0f, 0.0f, static_cast<float>(renderExtent_.width), static_cast<float>(renderExtent_.height), 0.0f, 1.0f};
	VkRect2D scissor{{0, 0}, renderExtent_};
	vkCmdSetViewport(cmd, 0, 1, &vp);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// トゥーンの輪郭線(反転した殻)。通常の描画より先に描く: 殻の裏面は元の面より奥なので、その後に描く本体が殻を覆い、
	// 外側にはみ出した縁だけが残る。自発光のもの(ランプ)・半透明のものは描かない(スキニングのモデルの半透明の材質は、シェーダー側で消す)
	bool anyMToon = false; // MToonの材質(輪郭線が材質ごとに決まる)を持つモデルがあるか
	for(const auto& command : commands){
		anyMToon = anyMToon || (command.owner && command.owner->hasMToon());
	}
	if((isToonShading() && outlineWidth_ > 0.0f) || anyMToon){
		float outlinePush[24];
		outlinePush[20] = isToonShading() ? 1.0f : 0.0f;
		outlinePush[21] = outlinePush[22] = outlinePush[23] = 0.0f;
		outlinePush[16] = 2.0f / static_cast<float>(renderExtent_.width);
		outlinePush[17] = 2.0f / static_cast<float>(renderExtent_.height);
		outlinePush[18] = outlineWidth_;
		outlinePush[19] = 0.0006f; // 殻を奥へずらす量(クリップ空間の深度)
		// 描かない/省ける輪郭線を、CPU側で見分けて描画自体を省く:
		//  - 輪郭線の無い材質(MToonの輪郭線なし)
		//  - MToonの輪郭線(ワールド座標の太さ)が、画面上で0.3ピクセル未満のもの(遠くのモデル。見えないので描かない)
		// 描くものは、アルファの切り抜きが要らないもの(不透明なテクスチャ)と要るものでパスを分ける。
		// 前者はフラグメントシェーダーがテクスチャを読まず discard もしないので、TBDRのGPUで隠れた面の除去が効く
		const float halfWidth = static_cast<float>(renderExtent_.width) * 0.5f;
		const float halfHeight = static_cast<float>(renderExtent_.height) * 0.5f;
		const auto wantsOutline = [&](size_t ci){
			const auto& command = commands[ci];
			const bool skinned = command.kind == DrawKind::LitSkinned;
			if((command.kind != DrawKind::Lit && !skinned) || culled[ci]){
				return false;
			}
			const int flags = static_cast<int>(command.params[2] + 0.5f);
			// トゥーンが無効のときは、MToonのモデルだけ(材質ごとの輪郭線)を描く
			if(((flags >> 1) & 127) != 0 || (!skinned && command.params[3] < 0.5f) || (skinned && !command.boneSet)
				|| (!isToonShading() && !(command.owner && command.owner->hasMToon()))){
				return false;
			}
			if(command.owner && command.materialIndex < command.owner->data().materials.size()){
				const auto& mtoon = command.owner->data().materials[command.materialIndex].mtoon;
				if(mtoon.outlineMode == 2){
					return false;
				}
				if(mtoon.outlineMode == 1 && outlineMinPixels_ > 0.0f){
					const float *mn = nullptr, *mx = nullptr;
					if(boundsOf(command, mn, mx)){
						const float* m = command.mvp.data(); // 列優先
						const float w = m[3] * (mn[0] + mx[0]) * 0.5f + m[7] * (mn[1] + mx[1]) * 0.5f + m[11] * (mn[2] + mx[2]) * 0.5f + m[15];
						if(w > 0.0f){
							// 殻はモデル空間で法線方向へ太さだけ動かす: クリップ空間でのずれ(xy) ≒ mvpの行ベクトルの長さ x 太さ
							const float dx = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
							const float dy = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
							const float pixels = std::max(dx * halfWidth, dy * halfHeight) * mtoon.outlineWidth / w;
							if(pixels < outlineMinPixels_){
								return false;
							}
						}
					}
				}
			}
			return true;
		};
		const auto isOpaqueOutline = [&](const DrawCommand& command){
			return command.params[3] >= 0.999f && command.texture && !command.texture->hasAlpha();
		};
		for(int alphaPass = 0; alphaPass < 2; ++alphaPass){
			VkPipeline bound = VK_NULL_HANDLE;
			for(size_t ci = 0; ci < commands.size(); ++ci){
				const auto& command = commands[ci];
				if(!wantsOutline(ci)){
					continue;
				}
				const bool opaque = isOpaqueOutline(command);
				if(opaque == (alphaPass == 1)){
					continue;
				}
				const bool skinned = command.kind == DrawKind::LitSkinned;
				VkPipeline pipeline = skinned ? (opaque ? pipes.outlineSkinOpaque : pipes.outlineSkin) : (opaque ? pipes.outlineOpaque : pipes.outline);
				if(pipeline != bound){
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					bound = pipeline;
				}
				const VkDescriptorSet outlineTexture = command.textureSetOverride ? command.textureSetOverride : command.texture->descriptorSet();
				if(skinned){
					const VkDescriptorSet sets[] = {command.boneSet, outlineTexture};
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outlineSkinLayout_, 0, 2, sets, 0, nullptr);
				}
				else{
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outlineLayout_, 1, 1, &outlineTexture, 0, nullptr);
				}
				const VkBuffer vb = vertexBufferOf(command);
				const VkDeviceSize offset = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
				vkCmdBindIndexBuffer(cmd, command.mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
				std::copy(command.mvp.data(), command.mvp.data() + 16, outlinePush);
				vkCmdPushConstants(cmd, skinned ? outlineSkinLayout_ : outlineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(outlinePush), outlinePush);
				vkCmdDrawIndexed(cmd, command.mesh->indexCount(), 1, 0, vertexOffsetOf(command), command.materialIndex);
				frame.meshes.push_back(command.mesh); // 描画中はメッシュを生かしておく(本体の描画でも積むが、重複してもよい)
			}
		}
	}

	stamp(kTsOutline);

	// 予約順に描画する。種類が変わるときだけパイプラインを切り替える
	// (push constantの範囲が違うパイプラインレイアウトへ切り替えるとdescriptor setの対応が外れるため、
	//  Litに切り替えたときはset0(フレームUBO)とset1(テクスチャ)を、スプライトは毎回set1をバインドし直す)
	VkPipeline boundScenePipeline = VK_NULL_HANDLE;
	int litDrawn = 0, opaqueDrawn = 0; // 描いたライティング付きの数と、そのうち不透明の版で描いた数(計測用)
	for(size_t ci = 0; ci < commands.size(); ++ci){
		const auto& command = commands[ci];
		const bool skinned = command.kind == DrawKind::LitSkinned;
		const bool lit = command.kind == DrawKind::Lit || skinned;
		if((skinned && !command.boneSet) || culled[ci]){
			continue;
		}
		VkPipelineLayout layout = skinned ? litSkinLayout_ : (lit ? litLayout_ : spriteLayout_);
		// ライティング付きは、不透明の材質(不透明度1・半透明のピクセル無し・材質モーフで不透明度が変わらない)なら、ブレンド・discardなしの版で描く
		// アルファの切り抜き(MASK)の材質は、切り抜き用(ブレンドなし・しきい値でdiscard)。しきい値はflagsのbit16〜
		const bool mask = lit && (static_cast<int>(command.params[2] + 0.5f) >> 16) > 0;
		const bool opaque = lit && !mask && command.params[3] >= 0.999f && command.texture && !command.texture->hasAlpha()
			&& !(command.owner && command.owner->hasMaterialMorph());
		litDrawn += lit ? 1 : 0;
		opaqueDrawn += (opaque || mask) ? 1 : 0;
		VkPipeline pipeline = skinned ? (mask ? pipes.litSkinMask : (opaque ? pipes.litSkinOpaque : pipes.litSkin))
			: (lit ? (mask ? pipes.litMask : (opaque ? pipes.litOpaque : pipes.lit)) : (command.kind == DrawKind::Sprite2D ? pipes.sprite2d : pipes.sprite3d));
		if(pipeline != boundScenePipeline){
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			boundScenePipeline = pipeline;
		}

		const VkBuffer vb = vertexBufferOf(command);
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
		vkCmdBindIndexBuffer(cmd, command.mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

		const VkDescriptorSet textureSet = command.textureSetOverride ? command.textureSetOverride : command.texture->descriptorSet();
		if(lit){
			const VkDescriptorSet sets[] = {frame.descriptorSet, textureSet, command.boneSet};
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, skinned ? 3 : 2, sets, 0, nullptr);
			// push constant(128B): mvp(16) + モデル行列の上3行(12。行優先に詰め直す) + 材質(4)
			float push[32];
			std::copy(command.mvp.data(), command.mvp.data() + 16, push);
			const float* m = command.model.data(); // 列優先: m[col*4 + row]
			for(int row = 0; row < 3; ++row){
				for(int col = 0; col < 4; ++col){
					push[16 + row * 4 + col] = m[col * 4 + row];
				}
			}
			std::copy(command.params.begin(), command.params.end(), push + 28);
			vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
		}
		else{
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &textureSet, 0, nullptr);
			float push[24];
			std::copy(command.mvp.data(), command.mvp.data() + 16, push);
			std::copy(command.params.begin(), command.params.end(), push + 16);
			std::copy(command.uvRect.begin(), command.uvRect.end(), push + 20);
			vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
		}
		vkCmdDrawIndexed(cmd, command.mesh->indexCount(), 1, 0, vertexOffsetOf(command), command.materialIndex);

		// GPUが使い終わるまで(この枠のfence完了まで)メッシュとテクスチャを生かしておく
		frame.meshes.push_back(command.mesh);
		frame.textures.push_back(command.texture);
	}
	vkCmdEndRenderPass(cmd);
	stamp(kTsMain);

	// 3. 後処理: sceneColor_を全画面の三角形でスワップチェーンへ写す(FXAAなら、エッジをぼかしながら)
	{
		VkRenderPassBeginInfo postBegin{};
		postBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		postBegin.renderPass = postRenderPass_;
		postBegin.framebuffer = framebuffers_[index];
		postBegin.renderArea.extent = extent_;
		vkCmdBeginRenderPass(cmd, &postBegin, VK_SUBPASS_CONTENTS_INLINE);
		const VkViewport postViewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
		const VkRect2D postScissor{{0, 0}, extent_};
		vkCmdSetViewport(cmd, 0, 1, &postViewport);
		vkCmdSetScissor(cmd, 0, 1, &postScissor);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline_);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &postSet_, 0, nullptr);
		const bool useFxaa = antiAliasing_ == AntiAliasing::Fxaa || antiAliasing_ == AntiAliasing::Msaa4xFxaa;
		// xy=シーンの1テクセルの大きさ(uv)、z=FXAA、w=未使用 / xy=スワップチェーンの1ピクセルの大きさ(uv。フラグメント座標からuvを求める)
		const float postPush[8] = {1.0f / static_cast<float>(renderExtent_.width), 1.0f / static_cast<float>(renderExtent_.height), useFxaa ? 1.0f : 0.0f, 0.0f,
			1.0f / static_cast<float>(extent_.width), 1.0f / static_cast<float>(extent_.height), 0.0f, 0.0f};
		vkCmdPushConstants(cmd, postLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(postPush), postPush);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
	}
	stamp(kTsEnd);
	vkEndCommandBuffer(cmd);
	const uint64_t tRec1 = SDL_GetPerformanceCounter();

	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &frame.imageAvailable;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &renderFinished_[index];
	if(vkQueueSubmit(ctx_->queue(), 1, &submit, frame.inFlight) != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkQueueSubmit failed.");
		ready_ = false; // フェンスがリセット済みのまま待つと固まるので、以後の描画を止める
		return;
	}

	frame.queryPending = profile_;

	VkPresentInfoKHR present{};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &renderFinished_[index];
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain_;
	present.pImageIndices = &index;
	r = vkQueuePresentKHR(ctx_->queue(), &present);
	if(r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR){
		swapchainDirty_ = true;
	}
	frameIndex_ = (frameIndex_ + 1) % kMaxFramesInFlight;
	lastDrawCount_ = static_cast<int>(commands.size());
	lastCulledCount_ = culledCount;

	if(profile_){
		const uint64_t tEnd = SDL_GetPerformanceCounter();
		stats_.cpuWaitMs += toMs(tWait0, tWait1);
		stats_.cpuAcquireMs += toMs(tAcq0, tAcq1);
		if(lastSwapEnd_ != 0){
			stats_.cpuOutsideMs += toMs(lastSwapEnd_, tWait0);
		}
		lastSwapEnd_ = tEnd;
		stats_.cpuPrepareMs += toMs(tWait1, tPrep1);
		stats_.cpuRecordMs += toMs(tRec0, tRec1);
		stats_.cpuSubmitMs += toMs(tRec1, tEnd);
		stats_.drawCalls = static_cast<int>(commands.size());
		stats_.culled = culledCount;
		stats_.litDrawn = litDrawn;
		stats_.opaqueDrawn = opaqueDrawn;
		stats_.shadowCulled = shadowCulled;
		++stats_.frames;
		const uint64_t now = SDL_GetTicks();
		if(stats_.lastLog == 0){
			stats_.lastLog = now;
		}
		else if(now - stats_.lastLog >= 1000 && stats_.frames > 0){
			const double n = stats_.frames;
			const double g = stats_.gpuFrames > 0 ? stats_.gpuFrames : 1;
			const double gpuTotal = stats_.gpuMs[0] + stats_.gpuMs[1] + stats_.gpuMs[2] + stats_.gpuMs[3] + stats_.gpuMs[4] + stats_.gpuMs[5];
			SDL_Log("[profile] AA=%s cull=%s | %.1f fps | GPU %.2fms (skin %.2f, dirShadow %.2f, pointShadow %.2f, outline+main %.2f, post %.2f) | CPU outside-swap %.2f, wait %.2f, acquire %.2f, prepare %.2f, record %.2f, submit+present %.2f | draws %d (lit %d, opaque-pipeline %d; culled main %d, shadow %d/frame)",
				antiAliasingName(antiAliasing_), frustumCulling_ ? "on" : "off",
				n * 1000.0 / static_cast<double>(now - stats_.lastLog), gpuTotal / g,
				stats_.gpuMs[0] / g, stats_.gpuMs[1] / g, stats_.gpuMs[2] / g, (stats_.gpuMs[3] + stats_.gpuMs[4]) / g, stats_.gpuMs[5] / g, // Apple GPU(TBDR)は区間の境で処理が分かれないので、輪郭線と本体は合算で見る
				
				stats_.cpuOutsideMs / n, stats_.cpuWaitMs / n, stats_.cpuAcquireMs / n, stats_.cpuPrepareMs / n, stats_.cpuRecordMs / n, stats_.cpuSubmitMs / n, stats_.drawCalls, stats_.litDrawn, stats_.opaqueDrawn,
				stats_.culled, stats_.shadowCulled);
			stats_ = ProfileStats{};
			stats_.lastLog = now;
		}
	}
}

} // SDL_
