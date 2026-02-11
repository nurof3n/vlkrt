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
        static auto LoadShaderBytecode(const std::string& filename) -> std::vector<uint32_t>;
        static auto CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code) -> VkShaderModule;
    };
}  // namespace Vlkrt
