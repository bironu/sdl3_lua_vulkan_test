#include "vk/VulkanContext.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_vulkan.h>
#include <cstring>
#include <vector>

namespace
{

bool hasExtension(const std::vector<VkExtensionProperties>& props, const char* name)
{
	for(const auto& p : props){
		if(std::strcmp(p.extensionName, name) == 0){
			return true;
		}
	}
	return false;
}

} // namespace

VulkanContext::~VulkanContext()
{
	if(device_ != VK_NULL_HANDLE){
		vkDestroyDevice(device_, nullptr);
	}
	if(instance_ != VK_NULL_HANDLE){
		vkDestroyInstance(instance_, nullptr);
	}
}

bool VulkanContext::initInstance()
{
	Uint32 sdlExtCount = 0;
	const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
	if(!sdlExts){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_Vulkan_GetInstanceExtensions failed. %s", SDL_GetError());
		return false;
	}
	std::vector<const char*> instanceExts(sdlExts, sdlExts + sdlExtCount);

	// macOS(MoltenVK)は非準拠(portability)実装なので、列挙を許可する必要がある
	VkInstanceCreateFlags instanceFlags = 0;
	uint32_t availCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &availCount, nullptr);
	std::vector<VkExtensionProperties> availExts(availCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &availCount, availExts.data());
	if(hasExtension(availExts, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)){
		instanceExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "SDL3Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "None";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_2;

	VkInstanceCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.flags = instanceFlags;
	info.pApplicationInfo = &appInfo;
	info.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
	info.ppEnabledExtensionNames = instanceExts.data();
	if(VkResult r = vkCreateInstance(&info, nullptr, &instance_); r != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateInstance failed. VkResult=%d", static_cast<int>(r));
		return false;
	}
	return true;
}

bool VulkanContext::initDevice(VkSurfaceKHR surface)
{
	uint32_t devCount = 0;
	vkEnumeratePhysicalDevices(instance_, &devCount, nullptr);
	if(devCount == 0){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No Vulkan physical device found.");
		return false;
	}
	std::vector<VkPhysicalDevice> devices(devCount);
	vkEnumeratePhysicalDevices(instance_, &devCount, devices.data());

	// グラフィックス+present可能なキューを持ち、swapchainを使えるデバイス(discrete GPU優先)
	int bestScore = -1;
	for(VkPhysicalDevice dev : devices){
		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
		std::vector<VkExtensionProperties> exts(extCount);
		vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
		if(!hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)){
			continue;
		}

		uint32_t qCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
		std::vector<VkQueueFamilyProperties> queues(qCount);
		vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, queues.data());
		for(uint32_t i = 0; i < qCount; ++i){
			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
			if(!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present){
				continue;
			}
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(dev, &props);
			const int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1 : 0;
			if(score > bestScore){
				bestScore = score;
				physicalDevice_ = dev;
				queueFamily_ = i;
			}
			break;
		}
	}
	if(physicalDevice_ == VK_NULL_HANDLE){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No suitable Vulkan device (graphics+present+swapchain) found.");
		return false;
	}
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice_, &props);
	SDL_Log("Vulkan device: %s (API %u.%u.%u)", props.deviceName,
		VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));

	std::vector<const char*> deviceExts{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	uint32_t extCount = 0;
	vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
	std::vector<VkExtensionProperties> exts(extCount);
	vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, exts.data());
	// portabilityサブセット実装(MoltenVK)では有効化が必須
	if(hasExtension(exts, "VK_KHR_portability_subset")){
		deviceExts.push_back("VK_KHR_portability_subset");
	}

	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = queueFamily_;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	VkPhysicalDeviceFeatures features{};
	VkPhysicalDeviceFeatures supported;
	vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
	if(supported.imageCubeArray){
		features.imageCubeArray = VK_TRUE;
		hasCubeArray_ = true;
	}
	if(supported.samplerAnisotropy){
		features.samplerAnisotropy = VK_TRUE;
		maxAnisotropy_ = props.limits.maxSamplerAnisotropy;
	}
	VkDeviceCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queueInfo;
	info.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
	info.ppEnabledExtensionNames = deviceExts.data();
	info.pEnabledFeatures = &features;
	if(VkResult r = vkCreateDevice(physicalDevice_, &info, nullptr, &device_); r != VK_SUCCESS){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vkCreateDevice failed. VkResult=%d", static_cast<int>(r));
		return false;
	}
	vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
	return true;
}

uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
{
	VkPhysicalDeviceMemoryProperties mem;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
	for(uint32_t i = 0; i < mem.memoryTypeCount; ++i){
		if((typeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props){
			return i;
		}
	}
	return UINT32_MAX;
}

bool VulkanContext::submitOneShot(const std::function<void(VkCommandBuffer)>& record) const
{
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	poolInfo.queueFamilyIndex = queueFamily_;
	VkCommandPool pool = VK_NULL_HANDLE;
	if(vkCreateCommandPool(device_, &poolInfo, nullptr, &pool) != VK_SUCCESS){
		return false;
	}
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = pool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	bool ok = vkAllocateCommandBuffers(device_, &allocInfo, &cmd) == VK_SUCCESS;
	if(ok){
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &begin);
		record(cmd);
		vkEndCommandBuffer(cmd);
		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		ok = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(queue_) == VK_SUCCESS;
	}
	vkDestroyCommandPool(device_, pool, nullptr);
	return ok;
}
