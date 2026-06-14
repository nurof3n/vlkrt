#include "NRDDenoiser.h"

#include "Walnut/Application.h"
#include "Walnut/Core/Log.h"
#include "NRD.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Vlkrt
{
    namespace
    {
        constexpr nrd::Identifier kDenoiserId = 0;

        static void SetIdentity4x4(float* out16)
        {
            std::memset(out16, 0, sizeof(float) * 16);
            out16[0]  = 1.0f;
            out16[5]  = 1.0f;
            out16[10] = 1.0f;
            out16[15] = 1.0f;
        }

        static void Copy16(float* dst, const float* src) { std::memcpy(dst, src, sizeof(float) * 16); }

        static Walnut::ImageFormat GetWalnutFormat(nrd::Format format)
        {
            switch (format) {
                case nrd::Format::R8_UNORM:
                case nrd::Format::R8_SNORM:
                case nrd::Format::R8_UINT:
                case nrd::Format::R8_SINT:
                    // Fall back to RGBA or R32F if R8 is not supported. Walnut Image doesn't have an R8 format.
                    // We can use RGBA as a general fallback format.
                    return Walnut::ImageFormat::RGBA;

                case nrd::Format::RG8_UNORM:
                case nrd::Format::RG8_SNORM:
                case nrd::Format::RG8_UINT:
                case nrd::Format::RG8_SINT: return Walnut::ImageFormat::RGBA;

                case nrd::Format::RGBA8_UNORM:
                case nrd::Format::RGBA8_SNORM:
                case nrd::Format::RGBA8_UINT:
                case nrd::Format::RGBA8_SINT:
                case nrd::Format::RGBA8_SRGB: return Walnut::ImageFormat::RGBA;

                case nrd::Format::R16_UNORM:
                case nrd::Format::R16_SNORM:
                case nrd::Format::R16_UINT:
                case nrd::Format::R16_SINT:
                case nrd::Format::R16_SFLOAT: return Walnut::ImageFormat::R32F;

                case nrd::Format::RG16_UNORM:
                case nrd::Format::RG16_SNORM:
                case nrd::Format::RG16_UINT:
                case nrd::Format::RG16_SINT:
                case nrd::Format::RG16_SFLOAT: return Walnut::ImageFormat::RG16F;

                case nrd::Format::RGBA16_UNORM:
                case nrd::Format::RGBA16_SNORM:
                case nrd::Format::RGBA16_UINT:
                case nrd::Format::RGBA16_SINT:
                case nrd::Format::RGBA16_SFLOAT: return Walnut::ImageFormat::RGBA32F;

                case nrd::Format::R32_UINT:
                case nrd::Format::R32_SINT:
                case nrd::Format::R32_SFLOAT: return Walnut::ImageFormat::R32F;

                case nrd::Format::RG32_UINT:
                case nrd::Format::RG32_SINT:
                case nrd::Format::RG32_SFLOAT:
                    // Use RGBA32F since RG32F is not natively in Walnut::ImageFormat list
                    return Walnut::ImageFormat::RGBA32F;

                case nrd::Format::RGB32_UINT:
                case nrd::Format::RGB32_SINT:
                case nrd::Format::RGB32_SFLOAT: return Walnut::ImageFormat::RGBA32F;

                case nrd::Format::RGBA32_UINT:
                case nrd::Format::RGBA32_SINT:
                case nrd::Format::RGBA32_SFLOAT: return Walnut::ImageFormat::RGBA32F;

                default: return Walnut::ImageFormat::RGBA32F;
            }
        }

        static uint32_t FindMemoryTypeIndex(
                VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
                const bool typeSupported = (typeBits & (1u << i)) != 0;
                const bool flagsMatch    = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
                if (typeSupported && flagsMatch) return i;
            }

            return UINT32_MAX;
        }

        static VkSampler CreateNrdSampler(VkDevice device, nrd::Sampler samplerType)
        {
            VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.minLod           = 0.0f;
            samplerInfo.maxLod           = 0.0f;
            samplerInfo.maxAnisotropy    = 1.0f;
            samplerInfo.anisotropyEnable = VK_FALSE;

            const bool linear     = (samplerType == nrd::Sampler::LINEAR_CLAMP);
            samplerInfo.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            samplerInfo.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

            VkSampler sampler = VK_NULL_HANDLE;
            if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) return VK_NULL_HANDLE;

            return sampler;
        }
    }  // namespace

    void NRDDenoiser::DestroyDirectBackend()
    {
        for (auto& p : m_NrdPipelines) {
            if (p.Pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_Device, p.Pipeline, nullptr);
            if (p.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Device, p.PipelineLayout, nullptr);
            if (p.ResourceSetLayout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(m_Device, p.ResourceSetLayout, nullptr);
            if (p.ShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, p.ShaderModule, nullptr);
        }
        m_NrdPipelines.clear();

        for (VkSampler& s : m_NrdSamplers) {
            if (s != VK_NULL_HANDLE) {
                vkDestroySampler(m_Device, s, nullptr);
                s = VK_NULL_HANDLE;
            }
        }

        if (m_NrdSet0Layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_Device, m_NrdSet0Layout, nullptr);
            m_NrdSet0Layout = VK_NULL_HANDLE;
        }

        if (m_GuideViewZNrdImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_GuideViewZNrdImageView, nullptr);
            m_GuideViewZNrdImageView = VK_NULL_HANDLE;
        }
        if (m_GuideMotionVectorsNrdImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_GuideMotionVectorsNrdImageView, nullptr);
            m_GuideMotionVectorsNrdImageView = VK_NULL_HANDLE;
        }

        if (m_NrdConstantBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_NrdConstantBuffer, nullptr);
            m_NrdConstantBuffer = VK_NULL_HANDLE;
        }
        if (m_NrdConstantMemory != VK_NULL_HANDLE) {
            if (m_NrdConstantMapped) {
                vkUnmapMemory(m_Device, m_NrdConstantMemory);
                m_NrdConstantMapped = nullptr;
            }
            vkFreeMemory(m_Device, m_NrdConstantMemory, nullptr);
            m_NrdConstantMemory = VK_NULL_HANDLE;
        }
        m_NrdConstantCapacity = 0;

        if (m_NrdDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_NrdDescriptorPool, nullptr);
            m_NrdDescriptorPool = VK_NULL_HANDLE;
        }

        m_NrdSet0s.clear();
        m_BackendReady                 = false;
        m_Operational                  = false;
        m_ConstantSetIndex             = 0;
        m_ResourceSetIndex             = 1;
        m_FirstDispatchTransitionsDone = false;
        m_HasLastAppliedParams         = false;

        // Keep externally supplied guide buffers; renderer owns and updates these
        m_TransientPoolImages.clear();
        m_PermanentPoolImages.clear();
        m_OutDiffRadianceHitDist.reset();
        m_OutSpecRadianceHitDist.reset();
    }

    bool NRDDenoiser::BuildDirectBackend()
    {
        if (!m_DirectInstance) return false;

        DestroyDirectBackend();

        auto* instance                        = reinterpret_cast<nrd::Instance*>(m_DirectInstance);
        const nrd::InstanceDesc* instanceDesc = nrd::GetInstanceDesc(*instance);
        if (!instanceDesc) return false;

        m_ConstantSetIndex      = instanceDesc->constantBufferAndSamplersSpaceIndex;
        m_ResourceSetIndex      = instanceDesc->resourcesSpaceIndex;
        const bool validSetPair = ((m_ConstantSetIndex == 0 && m_ResourceSetIndex == 1)
                                   || (m_ConstantSetIndex == 1 && m_ResourceSetIndex == 0));
        if (!validSetPair) {
            WL_WARN_TAG("Renderer", "NRD backend supports only set indices 0 and 1 (any order). Got {}/{}.",
                    m_ConstantSetIndex, m_ResourceSetIndex);
            return false;
        }

        const uint32_t pipelineCount = instanceDesc->pipelinesNum;
        if (pipelineCount == 0) return false;

        const uint32_t MULTIPLIER = 32;

        VkDescriptorPoolSize poolSizes[4] = {};
        poolSizes[0]                      = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 };
        poolSizes[1]                      = { VK_DESCRIPTOR_TYPE_SAMPLER, instanceDesc->samplersNum * 64 };
        poolSizes[2]                      = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            std::max(32u, instanceDesc->descriptorPoolDesc.totalTexturesNum * MULTIPLIER + 8u) };
        poolSizes[3]                      = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            std::max(32u, instanceDesc->descriptorPoolDesc.totalStorageTexturesNum * MULTIPLIER + 8u) };

        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets       = (pipelineCount * MULTIPLIER) + 64;  // allow multiple dispatch instances per pipeline
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes    = poolSizes;
        if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_NrdDescriptorPool) != VK_SUCCESS) return false;

        // Ensure proper alignment (typically 256 bytes)
        VkPhysicalDeviceProperties deviceProps{};
        vkGetPhysicalDeviceProperties(Walnut::Application::GetPhysicalDevice(), &deviceProps);
        uint32_t align        = static_cast<uint32_t>(deviceProps.limits.minUniformBufferOffsetAlignment);
        uint32_t rawSize      = std::max(256u, instanceDesc->constantBufferMaxDataSize);
        m_NrdConstantStride   = (rawSize + align - 1) & ~(align - 1);
        m_NrdConstantCapacity = rawSize;

        {
            VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferInfo.size        = m_NrdConstantStride * 64;  // up to 64 dispatches
            bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_NrdConstantBuffer) != VK_SUCCESS) return false;

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(m_Device, m_NrdConstantBuffer, &req);

            VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocInfo.allocationSize  = req.size;
            allocInfo.memoryTypeIndex = FindMemoryTypeIndex(Walnut::Application::GetPhysicalDevice(),
                    req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (allocInfo.memoryTypeIndex == UINT32_MAX) return false;

            if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_NrdConstantMemory) != VK_SUCCESS) return false;
            if (vkBindBufferMemory(m_Device, m_NrdConstantBuffer, m_NrdConstantMemory, 0) != VK_SUCCESS) return false;

            if (vkMapMemory(m_Device, m_NrdConstantMemory, 0, req.size, 0, &m_NrdConstantMapped) != VK_SUCCESS) {
                m_NrdConstantMapped = nullptr;
                return false;
            }
        }

        // Vulkan SPIR-V register shifts
        const uint32_t SPIRV_SREG_OFFSET = 0;
        const uint32_t SPIRV_BREG_OFFSET = 2;
        const uint32_t SPIRV_UREG_OFFSET = 3;
        const uint32_t SPIRV_TREG_OFFSET = 20;

        std::vector<VkDescriptorSetLayoutBinding> set0Bindings;
        set0Bindings.reserve(1 + instanceDesc->samplersNum);

        VkDescriptorSetLayoutBinding cbBinding{};
        cbBinding.binding         = instanceDesc->constantBufferRegisterIndex + SPIRV_BREG_OFFSET;
        cbBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cbBinding.descriptorCount = 1;
        cbBinding.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        set0Bindings.push_back(cbBinding);

        for (uint32_t i = 0; i < instanceDesc->samplersNum; i++) {
            VkDescriptorSetLayoutBinding b{};
            b.binding         = instanceDesc->samplersBaseRegisterIndex + SPIRV_SREG_OFFSET + i;
            b.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
            b.descriptorCount = 1;
            b.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
            set0Bindings.push_back(b);
        }

        VkDescriptorSetLayoutCreateInfo set0LayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        set0LayoutInfo.bindingCount = static_cast<uint32_t>(set0Bindings.size());
        set0LayoutInfo.pBindings    = set0Bindings.data();
        if (vkCreateDescriptorSetLayout(m_Device, &set0LayoutInfo, nullptr, &m_NrdSet0Layout) != VK_SUCCESS)
            return false;

        {
            m_NrdSet0s.resize(64);
            std::vector<VkDescriptorSetLayout> layouts(64, m_NrdSet0Layout);
            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocInfo.descriptorPool     = m_NrdDescriptorPool;
            allocInfo.descriptorSetCount = 64;
            allocInfo.pSetLayouts        = layouts.data();
            if (vkAllocateDescriptorSets(m_Device, &allocInfo, m_NrdSet0s.data()) != VK_SUCCESS) return false;
        }

        for (uint32_t i = 0; i < instanceDesc->samplersNum && i < 2; i++) {
            m_NrdSamplers[i] = CreateNrdSampler(m_Device, instanceDesc->samplers[i]);
            if (m_NrdSamplers[i] == VK_NULL_HANDLE) return false;
        }

        std::vector<VkDescriptorBufferInfo> cbInfos(64);
        std::vector<VkWriteDescriptorSet> set0Writes;
        set0Writes.reserve(64 * (1 + instanceDesc->samplersNum));

        std::vector<std::vector<VkDescriptorImageInfo> > allSamplerInfos(
                64, std::vector<VkDescriptorImageInfo>(instanceDesc->samplersNum));

        for (uint32_t dIdx = 0; dIdx < 64; dIdx++) {
            cbInfos[dIdx].buffer = m_NrdConstantBuffer;
            cbInfos[dIdx].offset = static_cast<VkDeviceSize>(dIdx) * m_NrdConstantStride;
            cbInfos[dIdx].range  = m_NrdConstantCapacity;

            VkWriteDescriptorSet cbWrite = {};
            cbWrite.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cbWrite.dstSet               = m_NrdSet0s[dIdx];
            cbWrite.dstBinding           = instanceDesc->constantBufferRegisterIndex + SPIRV_BREG_OFFSET;
            cbWrite.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            cbWrite.descriptorCount      = 1;
            cbWrite.pBufferInfo          = &cbInfos[dIdx];
            set0Writes.push_back(cbWrite);

            for (uint32_t i = 0; i < instanceDesc->samplersNum; i++) {
                allSamplerInfos[dIdx][i].sampler = (i < 2) ? m_NrdSamplers[i] : m_NrdSamplers[0];

                VkWriteDescriptorSet samplerWrite = {};
                samplerWrite.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                samplerWrite.dstSet               = m_NrdSet0s[dIdx];
                samplerWrite.dstBinding           = instanceDesc->samplersBaseRegisterIndex + SPIRV_SREG_OFFSET + i;
                samplerWrite.descriptorType       = VK_DESCRIPTOR_TYPE_SAMPLER;
                samplerWrite.descriptorCount      = 1;
                samplerWrite.pImageInfo           = &allSamplerInfos[dIdx][i];
                set0Writes.push_back(samplerWrite);
            }
        }

        vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(set0Writes.size()), set0Writes.data(), 0, nullptr);

        auto zeroImage = [](const std::shared_ptr<Walnut::Image>& image) {
            if (!image) return;

            const size_t bytes = static_cast<size_t>(image->GetWidth()) * static_cast<size_t>(image->GetHeight())
                                 * sizeof(float) * 4;
            std::vector<uint8_t> zeros(bytes, 0u);
            image->SetData(zeros.data());
        };

        // Create placeholder guide buffers for REBLUR mode
        // These will be populated from RT shader output later; for now they're initialized to zero/black
        if (m_InstanceWidth > 0 && m_InstanceHeight > 0) {
            // Create placeholder images for guide resources (all using RGBA32F for simplicity)
            // IN_NORMAL_ROUGHNESS: normals in RGB, roughness in A
            if (!m_GuideNormalRoughness) {
                m_GuideNormalRoughness = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_GuideNormalRoughness);
            }
            // IN_VIEWZ: view space depth in R
            if (!m_GuideViewZ) {
                m_GuideViewZ = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_GuideViewZ);
            }
            // IN_MV: motion vectors in RG
            if (!m_GuideMotionVectors) {
                m_GuideMotionVectors = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_GuideMotionVectors);
            }
            // IN_DIFF_RADIANCE_HITDIST: diffuse radiance in RGB, hit distance in A
            if (!m_GuideDiffRadianceHitDist) {
                m_GuideDiffRadianceHitDist = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_GuideDiffRadianceHitDist);
            }
            // IN_SPEC_RADIANCE_HITDIST: specular radiance in RGB, hit distance in A
            if (!m_GuideSpecRadianceHitDist) {
                m_GuideSpecRadianceHitDist = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_GuideSpecRadianceHitDist);
            }

            // Create separate OUT images because NRD does not support in-place writing
            if (!m_OutDiffRadianceHitDist) {
                m_OutDiffRadianceHitDist = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_OutDiffRadianceHitDist);
            }
            if (!m_OutSpecRadianceHitDist) {
                m_OutSpecRadianceHitDist = std::make_shared<Walnut::Image>(
                        m_InstanceWidth, m_InstanceHeight, Walnut::ImageFormat::RGBA32F);
                zeroImage(m_OutSpecRadianceHitDist);
            }

            // Create NRD internal pools using the exact smaller formats requested by NRD
            m_PermanentPoolImages.resize(instanceDesc->permanentPoolSize);
            for (uint32_t i = 0; i < instanceDesc->permanentPoolSize; i++) {
                const nrd::TextureDesc& texDesc = instanceDesc->permanentPool[i];
                const uint32_t ds               = std::max(1u, static_cast<uint32_t>(texDesc.downsampleFactor));
                const uint32_t w                = std::max(1u, m_InstanceWidth / ds);
                const uint32_t h                = std::max(1u, m_InstanceHeight / ds);
                const Walnut::ImageFormat fmt   = GetWalnutFormat(texDesc.format);

                m_PermanentPoolImages[i] = std::make_shared<Walnut::Image>(w, h, fmt);
                zeroImage(m_PermanentPoolImages[i]);
            }

            m_TransientPoolImages.resize(instanceDesc->transientPoolSize);
            for (uint32_t i = 0; i < instanceDesc->transientPoolSize; i++) {
                const nrd::TextureDesc& texDesc = instanceDesc->transientPool[i];
                const uint32_t ds               = std::max(1u, static_cast<uint32_t>(texDesc.downsampleFactor));
                const uint32_t w                = std::max(1u, m_InstanceWidth / ds);
                const uint32_t h                = std::max(1u, m_InstanceHeight / ds);
                const Walnut::ImageFormat fmt   = GetWalnutFormat(texDesc.format);

                m_TransientPoolImages[i] = std::make_shared<Walnut::Image>(w, h, fmt);
                zeroImage(m_TransientPoolImages[i]);
            }
        }

        if (m_GuideViewZ) {
            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image    = m_GuideViewZ->GetVkImage();
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format   = VK_FORMAT_R32G32B32A32_SFLOAT;
            // g_guideViewZ layout: R=depth(viewZ), GBA=albedo.xyz
            // NRD reads IN_VIEWZ from the R channel - use IDENTITY swizzle.
            viewInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_GuideViewZNrdImageView) != VK_SUCCESS) return false;
        }


        if (m_GuideMotionVectors) {
            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image                           = m_GuideMotionVectors->GetVkImage();
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = VK_FORMAT_R32G32B32A32_SFLOAT;
            viewInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_GuideMotionVectorsNrdImageView) != VK_SUCCESS)
                return false;
        }

        m_NrdPipelines.resize(pipelineCount);
        for (uint32_t pi = 0; pi < pipelineCount; pi++) {
            const nrd::PipelineDesc& nrdPipeline = instanceDesc->pipelines[pi];
            NrdPipelineState& state              = m_NrdPipelines[pi];

            if (!nrdPipeline.computeShaderSPIRV.bytecode || nrdPipeline.computeShaderSPIRV.size == 0) return false;

            VkShaderModuleCreateInfo smInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            smInfo.codeSize = static_cast<size_t>(nrdPipeline.computeShaderSPIRV.size);
            smInfo.pCode    = reinterpret_cast<const uint32_t*>(nrdPipeline.computeShaderSPIRV.bytecode);
            if (vkCreateShaderModule(m_Device, &smInfo, nullptr, &state.ShaderModule) != VK_SUCCESS) return false;

            std::vector<VkDescriptorSetLayoutBinding> resourceBindings;
            uint32_t baseTextureRegister = instanceDesc->resourcesBaseRegisterIndex;
            uint32_t baseStorageRegister = instanceDesc->resourcesBaseRegisterIndex;
            for (uint32_t ri = 0; ri < nrdPipeline.resourceRangesNum; ri++) {
                const nrd::ResourceRangeDesc& range = nrdPipeline.resourceRanges[ri];
                const bool isTexture                = (range.descriptorType == nrd::DescriptorType::TEXTURE);
                const bool isStorage                = (range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE);

                if (!isTexture && !isStorage) return false;

                uint32_t baseBinding = isTexture ? (baseTextureRegister + SPIRV_TREG_OFFSET)
                                                 : (baseStorageRegister + SPIRV_UREG_OFFSET);

                VkDescriptorType vkType
                        = isTexture ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                for (uint32_t bi = 0; bi < range.descriptorsNum; bi++) {
                    uint32_t slot = baseBinding + bi;
                    VkDescriptorSetLayoutBinding b{};
                    b.binding         = slot;
                    b.descriptorType  = vkType;
                    b.descriptorCount = 1;
                    b.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
                    resourceBindings.push_back(b);
                    state.ResourceSlots.push_back({ slot, vkType });
                }

                if (isTexture)
                    baseTextureRegister += range.descriptorsNum;
                else
                    baseStorageRegister += range.descriptorsNum;
            }

            VkDescriptorSetLayoutCreateInfo resourcesLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            resourcesLayoutInfo.bindingCount = static_cast<uint32_t>(resourceBindings.size());
            resourcesLayoutInfo.pBindings    = resourceBindings.data();
            if (vkCreateDescriptorSetLayout(m_Device, &resourcesLayoutInfo, nullptr, &state.ResourceSetLayout)
                    != VK_SUCCESS)
                return false;

            {
                // Pre-allocate multiple sets per pipeline to handle multiple invocations
                // of the same pipeline (e.g., REBLUR blur passes) without overwriting
                // executing descriptor sets mid-flight.
                state.ResourceSets.resize(MULTIPLIER, VK_NULL_HANDLE);
                std::vector<VkDescriptorSetLayout> layouts(MULTIPLIER, state.ResourceSetLayout);

                VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocInfo.descriptorPool     = m_NrdDescriptorPool;
                allocInfo.descriptorSetCount = MULTIPLIER;
                allocInfo.pSetLayouts        = layouts.data();
                if (vkAllocateDescriptorSets(m_Device, &allocInfo, state.ResourceSets.data()) != VK_SUCCESS)
                    return false;
            }

            VkDescriptorSetLayout setLayouts[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
            setLayouts[m_ConstantSetIndex]      = m_NrdSet0Layout;
            setLayouts[m_ResourceSetIndex]      = state.ResourceSetLayout;
            VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plInfo.setLayoutCount = 2;
            plInfo.pSetLayouts    = setLayouts;
            if (vkCreatePipelineLayout(m_Device, &plInfo, nullptr, &state.PipelineLayout) != VK_SUCCESS) return false;

            VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = state.ShaderModule;
            stageInfo.pName  = instanceDesc->shaderEntryPoint;

            VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            cpInfo.stage  = stageInfo;
            cpInfo.layout = state.PipelineLayout;
            if (vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &state.Pipeline) != VK_SUCCESS)
                return false;
        }

        m_BackendReady = true;
        return true;
    }

    bool NRDDenoiser::ExecutePreparedDispatches(VkCommandBuffer cmd, const std::shared_ptr<Walnut::Image>& ioImage,
            const void* dispatchesOpaque, uint32_t dispatchesNum)
    {
        if (!m_BackendReady || !dispatchesOpaque || !ioImage) return false;

        const nrd::DispatchDesc* dispatches = reinterpret_cast<const nrd::DispatchDesc*>(dispatchesOpaque);
        bool executedAny                    = false;

        // Transition ALL internal pools and out textures to GENERAL before the first dispatch
        // oldLayout = UNDEFINED because Walnut::Image may not have been used as a storage image yet
        if (!m_FirstDispatchTransitionsDone) {
            std::vector<VkImageMemoryBarrier> initialBarriers;
            auto addBarrier = [&](const std::shared_ptr<Walnut::Image>& img) {
                if (!img) return;
                VkImageMemoryBarrier b            = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                b.image                           = img->GetVkImage();
                b.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;  // discard previous content
                b.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
                b.srcAccessMask                   = 0;
                b.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.baseMipLevel   = 0;
                b.subresourceRange.levelCount     = 1;
                b.subresourceRange.baseArrayLayer = 0;
                b.subresourceRange.layerCount     = 1;
                initialBarriers.push_back(b);
            };

            addBarrier(m_OutDiffRadianceHitDist);
            addBarrier(m_OutSpecRadianceHitDist);
            for (const auto& poolImg : m_TransientPoolImages) addBarrier(poolImg);
            for (const auto& poolImg : m_PermanentPoolImages) addBarrier(poolImg);

            if (!initialBarriers.empty()) {
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                        nullptr, 0, nullptr, static_cast<uint32_t>(initialBarriers.size()), initialBarriers.data());
            }

            m_FirstDispatchTransitionsDone = true;
        }

        // Reset resource set allocators for each pipeline
        m_CurrentSet0Index = 0;
        for (NrdPipelineState& pipe : m_NrdPipelines) { pipe.CurrentSetIndex = 0; }

        // Resource dependency tracking
        struct ResourceAccess
        {
            nrd::ResourceType type;
            uint16_t index;
            bool written;
        };
        std::vector<ResourceAccess> writtenResources;

        for (uint32_t di = 0; di < dispatchesNum; di++) {
            const nrd::DispatchDesc& d = dispatches[di];
            if (d.pipelineIndex >= m_NrdPipelines.size()) continue;

            NrdPipelineState& p = m_NrdPipelines[d.pipelineIndex];

            // Check if this pass reads or writes any resource that was previously written
            bool needsBarrier = false;
            for (uint32_t ri = 0; ri < d.resourcesNum; ri++) {
                if (ri >= p.ResourceSlots.size()) break;
                const nrd::ResourceDesc& r = d.resources[ri];

                for (const auto& wa : writtenResources) {
                    if (wa.type == r.type && wa.index == r.indexInPool) {
                        needsBarrier = true;
                        break;
                    }
                }
                if (needsBarrier) break;
            }

            if (needsBarrier && executedAny) {
                VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                memoryBarrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
                memoryBarrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                        1, &memoryBarrier, 0, nullptr, 0, nullptr);

                // Clear tracker after barrier since everything is now synchronized
                writtenResources.clear();
            }

            // Track any writes performed by this dispatch for subsequent dispatches
            for (uint32_t ri = 0; ri < d.resourcesNum; ri++) {
                if (ri >= p.ResourceSlots.size()) break;
                const nrd::ResourceDesc& r = d.resources[ri];
                if (r.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE) {
                    writtenResources.push_back({ r.type, r.indexInPool, true });
                }
            }

            if (p.CurrentSetIndex >= p.ResourceSets.size()) {
                WL_WARN_TAG("Renderer", "Insufficient descriptor sets for NRD pipeline {}! (Index {}, Size {})",
                        d.pipelineIndex, p.CurrentSetIndex, p.ResourceSets.size());
                continue;
            }
            VkDescriptorSet currentResourceSet = p.ResourceSets[p.CurrentSetIndex++];

            auto getImageViewForResource = [this](const nrd::ResourceDesc& resource,
                                                   const std::shared_ptr<Walnut::Image>& image) -> VkImageView {
                if (resource.type == nrd::ResourceType::IN_VIEWZ && m_GuideViewZNrdImageView != VK_NULL_HANDLE)
                    return m_GuideViewZNrdImageView;
                if (resource.type == nrd::ResourceType::IN_MV && m_GuideMotionVectorsNrdImageView != VK_NULL_HANDLE)
                    return m_GuideMotionVectorsNrdImageView;

                return image ? image->GetVkImageView() : VK_NULL_HANDLE;
            };

            auto getImageForResource
                    = [this, &ioImage](const nrd::ResourceDesc& resource) -> std::shared_ptr<Walnut::Image> {
                auto ensurePoolImage = [this, &ioImage](std::vector<std::shared_ptr<Walnut::Image> >& pool,
                                               uint32_t index, bool isPermanent) -> std::shared_ptr<Walnut::Image> {
                    if (index >= pool.size()) pool.resize(index + 1);

                    if (!pool[index]) {
                        auto* instance                        = reinterpret_cast<nrd::Instance*>(m_DirectInstance);
                        const nrd::InstanceDesc* instanceDesc = nrd::GetInstanceDesc(*instance);

                        nrd::Format nrdFmt        = nrd::Format::RGBA32_SFLOAT;
                        uint32_t downsampleFactor = 1;
                        if (instanceDesc) {
                            if (isPermanent && index < instanceDesc->permanentPoolSize) {
                                nrdFmt = instanceDesc->permanentPool[index].format;
                                downsampleFactor
                                        = std::max(1u, (uint32_t) instanceDesc->permanentPool[index].downsampleFactor);
                            }
                            else if (!isPermanent && index < instanceDesc->transientPoolSize) {
                                nrdFmt = instanceDesc->transientPool[index].format;
                                downsampleFactor
                                        = std::max(1u, (uint32_t) instanceDesc->transientPool[index].downsampleFactor);
                            }
                        }

                        const uint32_t w              = std::max(1u, ioImage->GetWidth() / downsampleFactor);
                        const uint32_t h              = std::max(1u, ioImage->GetHeight() / downsampleFactor);
                        const Walnut::ImageFormat fmt = GetWalnutFormat(nrdFmt);
                        pool[index]                   = std::make_shared<Walnut::Image>(w, h, fmt);
                        const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(float) * 4;
                        std::vector<uint8_t> zeros(bytes, 0u);
                        pool[index]->SetData(zeros.data());
                    }

                    return pool[index];
                };

                const nrd::ResourceType type = resource.type;
                switch (type) {
                    case nrd::ResourceType::IN_SIGNAL:
                    case nrd::ResourceType::OUT_SIGNAL: return ioImage;

                    case nrd::ResourceType::IN_NORMAL_ROUGHNESS: return m_GuideNormalRoughness;
                    case nrd::ResourceType::IN_VIEWZ: return m_GuideViewZ;
                    case nrd::ResourceType::IN_MV: return m_GuideMotionVectors;

                    case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
                    case nrd::ResourceType::IN_DIFF_HITDIST:
                    case nrd::ResourceType::IN_DIFF_DIRECTION_HITDIST: return m_GuideDiffRadianceHitDist;

                    case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
                    case nrd::ResourceType::OUT_DIFF_HITDIST:
                    case nrd::ResourceType::OUT_DIFF_DIRECTION_HITDIST: return m_OutDiffRadianceHitDist;

                    case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
                    case nrd::ResourceType::IN_SPEC_HITDIST: return m_GuideSpecRadianceHitDist;

                    case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
                    case nrd::ResourceType::OUT_SPEC_HITDIST: return m_OutSpecRadianceHitDist;

                    case nrd::ResourceType::TRANSIENT_POOL:
                        return ensurePoolImage(
                                m_TransientPoolImages, static_cast<uint32_t>(resource.indexInPool), false);

                    case nrd::ResourceType::PERMANENT_POOL:
                        return ensurePoolImage(
                                m_PermanentPoolImages, static_cast<uint32_t>(resource.indexInPool), true);

                    default: return nullptr;
                }
            };

            std::vector<VkDescriptorImageInfo> imageInfos(d.resourcesNum);
            std::vector<VkWriteDescriptorSet> writes;
            writes.reserve(d.resourcesNum);

            for (uint32_t ri = 0; ri < d.resourcesNum; ri++) {
                if (ri >= p.ResourceSlots.size()) break;
                const nrd::ResourceDesc& r = d.resources[ri];
                const auto& slot           = p.ResourceSlots[ri];

                const std::shared_ptr<Walnut::Image> targetImage   = getImageForResource(r);
                const std::shared_ptr<Walnut::Image> resolvedImage = targetImage ? targetImage : ioImage;

                imageInfos[ri].imageView   = getImageViewForResource(r, resolvedImage);
                imageInfos[ri].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkWriteDescriptorSet w = {};
                w.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet               = currentResourceSet;
                w.dstBinding           = slot.binding;
                w.descriptorType       = slot.type;
                w.descriptorCount      = 1;
                w.pImageInfo           = &imageInfos[ri];
                writes.push_back(w);
            }

            if (!writes.empty())
                vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            if (d.constantBufferData && d.constantBufferDataSize > 0
                    && d.constantBufferDataSize <= m_NrdConstantCapacity && m_NrdConstantMapped != nullptr) {
                if (m_CurrentSet0Index >= 64) {
                    WL_WARN_TAG("Renderer", "Exceeded max frame dispatches for constant buffers (64)");
                    m_CurrentSet0Index = 0;
                }

                VkDeviceSize offset = static_cast<VkDeviceSize>(m_CurrentSet0Index) * m_NrdConstantStride;
                uint8_t* dst        = static_cast<uint8_t*>(m_NrdConstantMapped) + offset;
                std::memcpy(dst, d.constantBufferData, d.constantBufferDataSize);
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.Pipeline);
            VkDescriptorSet sets[2]  = { VK_NULL_HANDLE, VK_NULL_HANDLE };
            sets[m_ConstantSetIndex] = m_NrdSet0s[m_CurrentSet0Index];
            sets[m_ResourceSetIndex] = currentResourceSet;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.PipelineLayout, 0, 2, sets, 0, nullptr);
            vkCmdDispatch(cmd, d.gridWidth, d.gridHeight, 1);
            executedAny = true;
            m_CurrentSet0Index++;
        }

        return executedAny;
    }

    bool NRDDenoiser::EnsureDirectInstance(uint32_t width, uint32_t height)
    {
        if (!m_RuntimeLinked) return false;

        if (m_BackendInitFailed && m_InstanceWidth == width && m_InstanceHeight == height) return false;

        if (m_InstanceWidth != width || m_InstanceHeight != height) m_BackendInitFailed = false;

        const bool needsRecreate
                = (m_DirectInstance == nullptr) || (m_InstanceWidth != width) || (m_InstanceHeight != height);
        if (!needsRecreate) return m_DirectInstanceReady;

        if (m_DirectInstance) {
            DestroyDirectBackend();
            auto* oldInstance = reinterpret_cast<nrd::Instance*>(m_DirectInstance);
            nrd::DestroyInstance(*oldInstance);
            m_DirectInstance         = nullptr;
            m_DirectInstanceReady    = false;
            m_LastPreparedDispatches = 0;
            m_HasLastAppliedParams   = false;
        }

        nrd::DenoiserDesc denoiserDesc{};
        denoiserDesc.identifier = kDenoiserId;
        denoiserDesc.denoiser   = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;

        nrd::InstanceCreationDesc instanceCreationDesc{};
        instanceCreationDesc.denoisers    = &denoiserDesc;
        instanceCreationDesc.denoisersNum = 1;

        nrd::Instance* instance     = nullptr;
        const nrd::Result createRes = nrd::CreateInstance(instanceCreationDesc, instance);
        if (createRes != nrd::Result::SUCCESS || !instance) {
            WL_WARN_TAG("Renderer", "Direct NRD CreateInstance failed for {}x{}.", width, height);
            return false;
        }

        nrd::RelaxSettings relaxSettings{};
        relaxSettings.hitDistanceReconstructionMode      = nrd::HitDistanceReconstructionMode::AREA_3X3;
        relaxSettings.minMaterialForDiffuse              = 0.0f;
        relaxSettings.minMaterialForSpecular             = 0.0f;
        relaxSettings.diffusePrepassBlurRadius           = 6.0f;
        relaxSettings.specularPrepassBlurRadius          = 10.0f;
        relaxSettings.minHitDistanceWeight               = 0.02f;
        relaxSettings.lobeAngleFraction                  = 0.15f;
        relaxSettings.roughnessFraction                  = 0.15f;
        relaxSettings.enableAntiFirefly                  = true;
        relaxSettings.diffuseMaxAccumulatedFrameNum      = 10;
        relaxSettings.specularMaxAccumulatedFrameNum     = 12;
        relaxSettings.diffuseMaxFastAccumulatedFrameNum  = 2;
        relaxSettings.specularMaxFastAccumulatedFrameNum = 3;
        relaxSettings.antilagSettings.accelerationAmount = 0.9f;
        relaxSettings.antilagSettings.spatialSigmaScale  = 2.5f;
        relaxSettings.antilagSettings.temporalSigmaScale = 0.22f;
        relaxSettings.antilagSettings.resetAmount        = 1.0f;
        const nrd::Result settingsRes = nrd::SetDenoiserSettings(*instance, kDenoiserId, &relaxSettings);
        if (settingsRes != nrd::Result::SUCCESS) {
            WL_WARN_TAG("Renderer", "Direct NRD SetDenoiserSettings failed. Destroying instance.");
            nrd::DestroyInstance(*instance);
            return false;
        }

        m_DirectInstance = instance;
        m_InstanceWidth  = width;
        m_InstanceHeight = height;
        if (!BuildDirectBackend()) {
            WL_WARN_TAG("Renderer", "Direct NRD backend build failed. Destroying instance.");
            nrd::DestroyInstance(*instance);
            m_DirectInstance      = nullptr;
            m_DirectInstanceReady = false;
            m_BackendInitFailed   = true;
            m_InstanceWidth       = 0;
            m_InstanceHeight      = 0;
            return false;
        }

        m_DirectInstanceReady    = true;
        m_BackendInitFailed      = false;
        m_LastPreparedDispatches = 0;
        return true;
    }

    void NRDDenoiser::Initialize(VkDevice device)
    {
        m_Device                 = device;
        m_Enabled                = false;
        m_RuntimeLinked          = false;
        m_DirectInstanceReady    = false;
        m_BackendInitFailed      = false;
        m_InstanceWidth          = 0;
        m_InstanceHeight         = 0;
        m_LastPreparedDispatches = 0;
        m_DirectInstance         = nullptr;
        m_HasLastAppliedParams   = false;

        m_Operational = false;

        const nrd::LibraryDesc* libraryDesc = nrd::GetLibraryDesc();
        m_RuntimeLinked                     = (libraryDesc != nullptr);
        if (m_RuntimeLinked) {
            WL_INFO_TAG("Renderer", "NRD runtime probe successful (v{}.{}.{}).", NRD_VERSION_MAJOR, NRD_VERSION_MINOR,
                    NRD_VERSION_BUILD);
        }
        else {
            WL_WARN_TAG("Renderer", "NRD runtime probe failed.");
        }
    }

    void NRDDenoiser::Shutdown()
    {
        DestroyDirectBackend();
        if (m_DirectInstance) {
            auto* instance = reinterpret_cast<nrd::Instance*>(m_DirectInstance);
            nrd::DestroyInstance(*instance);
            m_DirectInstance = nullptr;
        }

        m_Device                 = VK_NULL_HANDLE;
        m_Enabled                = false;
        m_Operational            = false;
        m_DirectInstanceReady    = false;
        m_BackendInitFailed      = false;
        m_InstanceWidth          = 0;
        m_InstanceHeight         = 0;
        m_LastPreparedDispatches = 0;
        m_HasLastAppliedParams   = false;
    }

    void NRDDenoiser::SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
        if (!enabled) m_BackendInitFailed = false;
    }

    const char* NRDDenoiser::GetStatus() const
    {
        if (!m_Enabled) return "Disabled";
        if (m_Operational) return "Active";
        if (m_BackendReady) return "Direct NRD backend ready (reference mode resources only)";
        if (m_DirectInstanceReady) return "Direct NRD instance ready (dispatch execution backend pending)";
        if (m_RuntimeLinked) return "Runtime linked (direct dispatch pipeline pending)";
        return "Runtime not linked";
    }

    void NRDDenoiser::Dispatch(
            VkCommandBuffer cmd, const std::shared_ptr<Walnut::Image>& ioImage, const NRDDenoiseParams& params)
    {
        (void) cmd;
        (void) ioImage;
        (void) params;

        if (!m_Enabled) return;

        m_Operational = false;
        if (EnsureDirectInstance(params.Width, params.Height)) {
            auto* instance   = reinterpret_cast<nrd::Instance*>(m_DirectInstance);
            const uint16_t w = static_cast<uint16_t>(std::min<uint32_t>(params.Width, 65535u));
            const uint16_t h = static_cast<uint16_t>(std::min<uint32_t>(params.Height, 65535u));

            const bool relaxTuningChanged
                    = !m_HasLastAppliedParams
                      || m_LastAppliedParams.MinMaterialForDiffuse != params.MinMaterialForDiffuse
                      || m_LastAppliedParams.MinMaterialForSpecular != params.MinMaterialForSpecular
                      || m_LastAppliedParams.DiffusePrepassBlurRadius != params.DiffusePrepassBlurRadius
                      || m_LastAppliedParams.SpecularPrepassBlurRadius != params.SpecularPrepassBlurRadius
                      || m_LastAppliedParams.DiffuseMaxAccumulatedFrameNum != params.DiffuseMaxAccumulatedFrameNum
                      || m_LastAppliedParams.SpecularMaxAccumulatedFrameNum != params.SpecularMaxAccumulatedFrameNum
                      || m_LastAppliedParams.DiffuseMaxFastAccumulatedFrameNum
                                 != params.DiffuseMaxFastAccumulatedFrameNum
                      || m_LastAppliedParams.SpecularMaxFastAccumulatedFrameNum
                                 != params.SpecularMaxFastAccumulatedFrameNum
                      || m_LastAppliedParams.AntilagAccelerationAmount != params.AntilagAccelerationAmount
                      || m_LastAppliedParams.AntilagSpatialSigmaScale != params.AntilagSpatialSigmaScale
                      || m_LastAppliedParams.AntilagTemporalSigmaScale != params.AntilagTemporalSigmaScale
                      || m_LastAppliedParams.AntilagResetAmount != params.AntilagResetAmount;

            if (relaxTuningChanged) {
                nrd::RelaxSettings relaxSettings{};
                relaxSettings.hitDistanceReconstructionMode      = nrd::HitDistanceReconstructionMode::AREA_3X3;
                relaxSettings.minMaterialForDiffuse              = params.MinMaterialForDiffuse;
                relaxSettings.minMaterialForSpecular             = params.MinMaterialForSpecular;
                relaxSettings.diffusePrepassBlurRadius           = params.DiffusePrepassBlurRadius;
                relaxSettings.specularPrepassBlurRadius          = params.SpecularPrepassBlurRadius;
                relaxSettings.minHitDistanceWeight               = 0.02f;
                relaxSettings.lobeAngleFraction                  = 0.15f;
                relaxSettings.roughnessFraction                  = 0.15f;
                relaxSettings.enableAntiFirefly                  = true;
                relaxSettings.diffuseMaxAccumulatedFrameNum      = params.DiffuseMaxAccumulatedFrameNum;
                relaxSettings.specularMaxAccumulatedFrameNum     = params.SpecularMaxAccumulatedFrameNum;
                relaxSettings.diffuseMaxFastAccumulatedFrameNum  = params.DiffuseMaxFastAccumulatedFrameNum;
                relaxSettings.specularMaxFastAccumulatedFrameNum = params.SpecularMaxFastAccumulatedFrameNum;
                relaxSettings.antilagSettings.accelerationAmount = params.AntilagAccelerationAmount;
                relaxSettings.antilagSettings.spatialSigmaScale  = params.AntilagSpatialSigmaScale;
                relaxSettings.antilagSettings.temporalSigmaScale = params.AntilagTemporalSigmaScale;
                relaxSettings.antilagSettings.resetAmount        = params.AntilagResetAmount;

                const nrd::Result settingsRes = nrd::SetDenoiserSettings(*instance, kDenoiserId, &relaxSettings);
                if (settingsRes != nrd::Result::SUCCESS) {
                    WL_WARN_TAG("Renderer", "Direct NRD SetDenoiserSettings failed for runtime tuning update.");
                }
                else {
                    m_LastAppliedParams    = params;
                    m_HasLastAppliedParams = true;
                }
            }

            nrd::CommonSettings commonSettings{};
            if (params.HasValidMatrices) {
                Copy16(commonSettings.viewToClipMatrix, params.ViewToClip);
                Copy16(commonSettings.viewToClipMatrixPrev, params.ViewToClipPrev);
                Copy16(commonSettings.worldToViewMatrix, params.WorldToView);
                Copy16(commonSettings.worldToViewMatrixPrev, params.WorldToViewPrev);
            }
            else {
                SetIdentity4x4(commonSettings.viewToClipMatrix);
                SetIdentity4x4(commonSettings.viewToClipMatrixPrev);
                SetIdentity4x4(commonSettings.worldToViewMatrix);
                SetIdentity4x4(commonSettings.worldToViewMatrixPrev);
            }

            commonSettings.resourceSize[0]     = w;
            commonSettings.resourceSize[1]     = h;
            commonSettings.resourceSizePrev[0] = w;
            commonSettings.resourceSizePrev[1] = h;
            commonSettings.rectSize[0]         = w;
            commonSettings.rectSize[1]         = h;
            commonSettings.rectSizePrev[0]     = w;
            commonSettings.rectSizePrev[1]     = h;
            commonSettings.cameraJitter[0]     = params.CameraJitter[0];
            commonSettings.cameraJitter[1]     = params.CameraJitter[1];
            commonSettings.cameraJitterPrev[0] = params.CameraJitterPrev[0];
            commonSettings.cameraJitterPrev[1] = params.CameraJitterPrev[1];
            commonSettings.frameIndex          = params.FrameIndex;
            commonSettings.denoisingRange      = 500000.0f;
            // Stricter disocclusion rejection helps shadow edges shed stale history faster.
            commonSettings.disocclusionThreshold          = params.DisocclusionThreshold;
            commonSettings.disocclusionThresholdAlternate = params.DisocclusionThresholdAlternate;
            commonSettings.accumulationMode
                    = (params.FrameIndex == 0) ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;

            const nrd::Result commonRes = nrd::SetCommonSettings(*instance, commonSettings);
            if (commonRes == nrd::Result::SUCCESS) {
                const nrd::Identifier identifiers[] = { kDenoiserId };
                const nrd::DispatchDesc* dispatches = nullptr;
                uint32_t dispatchesNum              = 0;
                const nrd::Result dispatchRes
                        = nrd::GetComputeDispatches(*instance, identifiers, 1, dispatches, dispatchesNum);
                if (dispatchRes == nrd::Result::SUCCESS && dispatches != nullptr) {
                    m_LastPreparedDispatches = dispatchesNum;

                    m_Operational = ExecutePreparedDispatches(cmd, ioImage, dispatches, dispatchesNum);
                }
                else {
                    m_LastPreparedDispatches = 0;
                    WL_WARN_TAG("Renderer", "Direct NRD GetComputeDispatches failed for frame {}.", params.FrameIndex);
                }
            }
            else {
                WL_WARN_TAG("Renderer", "Direct NRD SetCommonSettings failed for frame {}.", params.FrameIndex);
            }
        }
    }

    void NRDDenoiser::SetGuideBuffers(const std::shared_ptr<Walnut::Image>& normalRoughness,
            const std::shared_ptr<Walnut::Image>& viewZ, const std::shared_ptr<Walnut::Image>& motionVectors,
            const std::shared_ptr<Walnut::Image>& diffRadianceHitDist,
            const std::shared_ptr<Walnut::Image>& specRadianceHitDist)
    {
        m_GuideNormalRoughness     = normalRoughness;
        m_GuideViewZ               = viewZ;
        m_GuideMotionVectors       = motionVectors;
        m_GuideDiffRadianceHitDist = diffRadianceHitDist;
        m_GuideSpecRadianceHitDist = specRadianceHitDist;

        if (m_Device == VK_NULL_HANDLE) return;

        // Recreate NRD-specific image views using the new guide textures
        if (m_GuideViewZNrdImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_GuideViewZNrdImageView, nullptr);
            m_GuideViewZNrdImageView = VK_NULL_HANDLE;
        }
        if (m_GuideMotionVectorsNrdImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_GuideMotionVectorsNrdImageView, nullptr);
            m_GuideMotionVectorsNrdImageView = VK_NULL_HANDLE;
        }

        if (m_GuideViewZ) {
            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image                           = m_GuideViewZ->GetVkImage();
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = VK_FORMAT_R32G32B32A32_SFLOAT;
            viewInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            vkCreateImageView(m_Device, &viewInfo, nullptr, &m_GuideViewZNrdImageView);
        }

        if (m_GuideMotionVectors) {
            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image                           = m_GuideMotionVectors->GetVkImage();
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = VK_FORMAT_R32G32B32A32_SFLOAT;
            viewInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            vkCreateImageView(m_Device, &viewInfo, nullptr, &m_GuideMotionVectorsNrdImageView);
        }
    }
}  // namespace Vlkrt
