#include "ShaderLoader.h"
#include "Utils.h"

#include <fstream>
#include <stdexcept>

namespace Vlkrt
{
    auto ShaderLoader::LoadShaderBytecode(const std::string& filename) -> std::vector<uint32_t>
    {
        auto filepath = Vlkrt::SHADERS_DIR + filename;
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);

        if (!file.is_open()) [[unlikely]] { throw std::runtime_error("Failed to open shader file: " + filepath); }

        size_t fileSize = (size_t) file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        file.seekg(0);
        file.read((char*) buffer.data(), fileSize);
        file.close();

        return buffer;
    }

    auto ShaderLoader::CreateShaderModule(VkDevice device, const std::vector<uint32_t>& bytecode) -> VkShaderModule
    {
        VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize                 = bytecode.size() * sizeof(uint32_t);
        createInfo.pCode                    = bytecode.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }

        return shaderModule;
    }
}  // namespace Vlkrt
