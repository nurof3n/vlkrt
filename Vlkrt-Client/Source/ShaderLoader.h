#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Vlkrt
{
	class ShaderLoader
	{
	public:
		static std::vector<uint32_t> LoadShaderSPIRV(const std::string& filepath);
		static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code);
	};
}  // namespace Vlkrt
