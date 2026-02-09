#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Vlkrt
{
    /**
     * @brief Class responsible for loading SPIR-V shader binaries and creating Vulkan shader modules.
     * 
     */
    class ShaderLoader
    {
    public:
        static std::vector<uint32_t> LoadShaderSPIRV(const std::string& filename);
        static VkShaderModule        CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code);
    };
}  // namespace Vlkrt
