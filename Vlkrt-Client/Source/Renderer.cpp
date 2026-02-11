#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "ShaderLoader.h"
#include "Utils.h"

#include "Walnut/Application.h"
#include "Walnut/VulkanRayTracing.h"
#include "Walnut/Core/Log.h"

#include <cstring>
#include <stdexcept>

namespace Vlkrt
{
    Renderer::Renderer()
    {
        m_Device                = Walnut::Application::GetDevice();
        m_RTPipelineProperties  = Walnut::Application::GetRayTracingPipelineProperties();
        m_AccelerationStructure = std::make_unique<AccelerationStructure>();
    }

    Renderer::~Renderer()
    {
        // Clean up Vulkan resources
        if (m_SBTBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_SBTBuffer, nullptr);
            vkFreeMemory(m_Device, m_SBTMemory, nullptr);
        }

        if (m_VertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_VertexBuffer, nullptr);
            vkFreeMemory(m_Device, m_VertexMemory, nullptr);
        }

        if (m_IndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_IndexBuffer, nullptr);
            vkFreeMemory(m_Device, m_IndexMemory, nullptr);
        }

        if (m_MaterialBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_MaterialBuffer, nullptr);
            vkFreeMemory(m_Device, m_MaterialMemory, nullptr);
        }

        if (m_MaterialIndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_MaterialIndexBuffer, nullptr);
            vkFreeMemory(m_Device, m_MaterialIndexMemory, nullptr);
        }

        if (m_DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);

        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);

        if (m_RTPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_Device, m_RTPipeline, nullptr);

        if (m_RTPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Device, m_RTPipelineLayout, nullptr);

        if (m_RaygenShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_RaygenShader, nullptr);
        if (m_MissShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_MissShader, nullptr);
        if (m_ClosestHitShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_ClosestHitShader, nullptr);
    }

    void Renderer::OnResize(uint32_t width, uint32_t height)
    {
        if (m_FinalImage) {
            if (m_FinalImage->GetWidth() == width && m_FinalImage->GetHeight() == height) return;

            m_FinalImage->Resize(width, height);
        }
        else {
            m_FinalImage = std::make_shared<Walnut::Image>(width, height, Walnut::ImageFormat::RGBA);
        }

        // (Re)create RT pipeline and descriptor sets
        if (m_RTPipeline == VK_NULL_HANDLE) {
            CreateDescriptorSets();
            CreateRayTracingPipeline();
            CreateShaderBindingTable();
        }
        else {
            // Update descriptor set with new image
            if (m_DescriptorSet != VK_NULL_HANDLE && m_FinalImage) {
                VkDescriptorImageInfo imageInfo = {};
                imageInfo.imageView             = m_FinalImage->GetVkImageView();
                imageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

                VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                writeDescriptorSet.dstSet               = m_DescriptorSet;
                writeDescriptorSet.dstBinding           = 1;
                writeDescriptorSet.dstArrayElement      = 0;
                writeDescriptorSet.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writeDescriptorSet.descriptorCount      = 1;
                writeDescriptorSet.pImageInfo           = &imageInfo;

                vkUpdateDescriptorSets(m_Device, 1, &writeDescriptorSet, 0, nullptr);
            }
        }

        // Image layout is now undefined after resize
        m_FirstFrame = true;
    }

    void Renderer::Render(const Scene& scene, const Camera& camera)
    {
        if (!m_FinalImage || (m_FinalImage->GetWidth() == 0 || m_FinalImage->GetHeight() == 0)) return;

        if (scene.StaticMeshes.empty() && scene.DynamicMeshes.empty()) return;

        m_ActiveScene  = &scene;
        m_ActiveCamera = &camera;

        // Calculate current scene metrics
        size_t totalMeshCount = scene.StaticMeshes.size() + scene.DynamicMeshes.size();
        size_t totalVertices  = 0;
        size_t totalIndices   = 0;
        for (const auto& mesh : scene.StaticMeshes) {
            totalVertices += mesh.Vertices.size();
            totalIndices += mesh.Indices.size();
        }
        for (const auto& mesh : scene.DynamicMeshes) {
            totalVertices += mesh.Vertices.size();
            totalIndices += mesh.Indices.size();
        }

        bool sizeChanged = (totalMeshCount != m_LastMeshCount) || (totalVertices != m_LastVertexCount)
                           || (totalIndices != m_LastIndexCount) || (scene.Materials.size() != m_LastMaterialCount);

        bool needsRebuild = !m_SceneValid || sizeChanged;

        // Create or update scene buffers on first run or when scene changes
        if (m_VertexBuffer == VK_NULL_HANDLE || needsRebuild) {
            if (needsRebuild && m_VertexBuffer != VK_NULL_HANDLE) {
                // Clean up old buffers if size changed
                if (totalMeshCount != m_LastMeshCount || totalVertices != m_LastVertexCount
                        || totalIndices != m_LastIndexCount) {
                    Walnut::Application::SubmitResourceFree(
                            [device = m_Device, vbuf = m_VertexBuffer, vmem = m_VertexMemory, ibuf = m_IndexBuffer,
                                    imem = m_IndexMemory, mibuf = m_MaterialIndexBuffer,
                                    mimem = m_MaterialIndexMemory]() {
                                if (vbuf) vkDestroyBuffer(device, vbuf, nullptr);
                                if (vmem) vkFreeMemory(device, vmem, nullptr);
                                if (ibuf) vkDestroyBuffer(device, ibuf, nullptr);
                                if (imem) vkFreeMemory(device, imem, nullptr);
                                if (mibuf) vkDestroyBuffer(device, mibuf, nullptr);
                                if (mimem) vkFreeMemory(device, mimem, nullptr);
                            });
                    m_VertexBuffer        = VK_NULL_HANDLE;
                    m_IndexBuffer         = VK_NULL_HANDLE;
                    m_MaterialIndexBuffer = VK_NULL_HANDLE;
                }
                if (scene.Materials.size() != m_LastMaterialCount) {
                    Walnut::Application::SubmitResourceFree(
                            [device = m_Device, buf = m_MaterialBuffer, mem = m_MaterialMemory]() {
                                if (buf) vkDestroyBuffer(device, buf, nullptr);
                                if (mem) vkFreeMemory(device, mem, nullptr);
                            });
                    m_MaterialBuffer = VK_NULL_HANDLE;
                }
                if (scene.Lights.size() != m_LastLightCount) {
                    Walnut::Application::SubmitResourceFree(
                            [device = m_Device, buf = m_LightBuffer, mem = m_LightMemory]() {
                                if (buf) vkDestroyBuffer(device, buf, nullptr);
                                if (mem) vkFreeMemory(device, mem, nullptr);
                            });
                    m_LightBuffer = VK_NULL_HANDLE;
                }
            }

            if (m_VertexBuffer == VK_NULL_HANDLE) CreateSceneBuffers(scene);

            UpdateSceneData(scene);

            // Store current state and mark scene as valid
            m_LastMeshCount     = totalMeshCount;
            m_LastVertexCount   = totalVertices;
            m_LastIndexCount    = totalIndices;
            m_LastMaterialCount = scene.Materials.size();
            m_LastLightCount    = scene.Lights.size();
            m_SceneValid        = true;
        }

        if (!m_AccelerationStructure->IsBuilt()) return;

        // Get command buffer
        VkCommandBuffer cmd = Walnut::Application::GetCommandBuffer(true);

        // Transition output image to GENERAL layout
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image                = m_FinalImage->GetVkImage();
        barrier.oldLayout     = m_FirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = m_FirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0,
                nullptr, 0, nullptr, 1, &barrier);

        // Bind ray tracing pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RTPipeline);

        // Bind descriptor sets
        vkCmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RTPipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

        // Push camera constants
        struct CameraData
        {
            glm::mat4 inverseView;
            glm::mat4 inverseProj;
            glm::vec3 position;
        } cameraData{};

        cameraData.inverseView = camera.GetInverseView();
        cameraData.inverseProj = camera.GetInverseProjection();
        cameraData.position    = camera.GetPosition();

        vkCmdPushConstants(cmd, m_RTPipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(CameraData), &cameraData);

        // Trace rays
        pvkCmdTraceRaysKHR(cmd, &m_RaygenRegion, &m_MissRegion, &m_HitRegion, &m_CallableRegion,
                m_FinalImage->GetWidth(), m_FinalImage->GetHeight(), 1);

        // Transition image back to SHADER_READ_ONLY for ImGui
        barrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Submit command buffer
        Walnut::Application::FlushCommandBuffer(cmd);

        m_FirstFrame = false;
    }

    void Renderer::CreateRayTracingPipeline()
    {
        // Load shaders
        auto raygenCode     = ShaderLoader::LoadShaderBytecode("raygen.rgen.spv");
        auto missCode       = ShaderLoader::LoadShaderBytecode("miss.rmiss.spv");
        auto closestHitCode = ShaderLoader::LoadShaderBytecode("closesthit.rchit.spv");

        m_RaygenShader     = ShaderLoader::CreateShaderModule(m_Device, raygenCode);
        m_MissShader       = ShaderLoader::CreateShaderModule(m_Device, missCode);
        m_ClosestHitShader = ShaderLoader::CreateShaderModule(m_Device, closestHitCode);

        // Shader stages
        VkPipelineShaderStageCreateInfo stages[3] = {};

        // Raygen
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stages[0].module = m_RaygenShader;
        stages[0].pName  = "main";

        // Miss
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
        stages[1].module = m_MissShader;
        stages[1].pName  = "main";

        // Closest hit
        stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stages[2].module = m_ClosestHitShader;
        stages[2].pName  = "main";

        // Shader groups
        VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};

        // Raygen group
        groups[0].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[0].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader      = 0;
        groups[0].closestHitShader   = VK_SHADER_UNUSED_KHR;
        groups[0].anyHitShader       = VK_SHADER_UNUSED_KHR;
        groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

        // Miss group
        groups[1].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[1].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader      = 1;
        groups[1].closestHitShader   = VK_SHADER_UNUSED_KHR;
        groups[1].anyHitShader       = VK_SHADER_UNUSED_KHR;
        groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

        // Hit group (triangles: closest hit only)
        groups[2].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[2].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].generalShader      = VK_SHADER_UNUSED_KHR;
        groups[2].closestHitShader   = 2;
        groups[2].anyHitShader       = VK_SHADER_UNUSED_KHR;
        groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

        // Create pipeline layout (push constants for camera)
        VkPushConstantRange pushConstant = {};
        pushConstant.stageFlags          = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        pushConstant.offset              = 0;
        pushConstant.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec3);  // inverseView, inverseProj, position

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount             = 1;
        pipelineLayoutInfo.pSetLayouts                = &m_DescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount     = 1;
        pipelineLayoutInfo.pPushConstantRanges        = &pushConstant;

        vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_RTPipelineLayout);

        // Create ray tracing pipeline
        VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
        pipelineInfo.sType                             = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount                        = 3;  // raygen + miss + closesthit
        pipelineInfo.pStages                           = stages;
        pipelineInfo.groupCount                        = 3;
        pipelineInfo.pGroups                           = groups;
        pipelineInfo.maxPipelineRayRecursionDepth      = 1;
        pipelineInfo.layout                            = m_RTPipelineLayout;

        pvkCreateRayTracingPipelinesKHR(
                m_Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_RTPipeline);
    }

    void Renderer::CreateShaderBindingTable()
    {
        uint32_t handleSize      = m_RTPipelineProperties.shaderGroupHandleSize;
        uint32_t handleAlignment = m_RTPipelineProperties.shaderGroupHandleAlignment;
        uint32_t groupCount      = 3;

        // Get shader group handles
        std::vector<uint8_t> handles(handleSize * groupCount);
        pvkGetRayTracingShaderGroupHandlesKHR(m_Device, m_RTPipeline, 0, groupCount, handles.size(), handles.data());

        // Calculate aligned sizes
        uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

        // Create SBT buffer
        VkDeviceSize sbtSize = handleSizeAligned * groupCount;
        m_SBTBuffer          = CreateBuffer(sbtSize,
                         VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_SBTMemory);

        // Map and copy handles
        void* data;
        vkMapMemory(m_Device, m_SBTMemory, 0, sbtSize, 0, &data);
        uint8_t* pData = (uint8_t*) data;
        for (uint32_t i = 0; i < groupCount; i++) {
            memcpy(pData, handles.data() + i * handleSize, handleSize);
            pData += handleSizeAligned;
        }
        vkUnmapMemory(m_Device, m_SBTMemory);

        // Get buffer device address
        VkBufferDeviceAddressInfo addressInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        addressInfo.buffer                    = m_SBTBuffer;
        VkDeviceAddress sbtAddress            = pvkGetBufferDeviceAddressKHR(m_Device, &addressInfo);

        // Setup regions
        m_RaygenRegion.deviceAddress = sbtAddress;
        m_RaygenRegion.stride        = handleSizeAligned;
        m_RaygenRegion.size          = handleSizeAligned;

        m_MissRegion.deviceAddress = sbtAddress + handleSizeAligned;
        m_MissRegion.stride        = handleSizeAligned;
        m_MissRegion.size          = handleSizeAligned;

        m_HitRegion.deviceAddress = sbtAddress + handleSizeAligned * 2;
        m_HitRegion.stride        = handleSizeAligned;
        m_HitRegion.size          = handleSizeAligned;

        m_CallableRegion = {};  // Not used
    }

    void Renderer::CreateDescriptorSets()
    {
        // Create descriptor set layout
        VkDescriptorSetLayoutBinding bindings[8] = {};

        // Binding 0: Acceleration structure
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        // Binding 1: Storage image
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        // Binding 2: Vertex buffer
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 3: Index buffer
        bindings[3].binding         = 3;
        bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 4: Material buffer
        bindings[4].binding         = 4;
        bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 5: Material index buffer
        bindings[5].binding         = 5;
        bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 6: Light buffer
        bindings[6].binding         = 6;
        bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 7: Textures
        bindings[7].binding         = 7;
        bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[7].descriptorCount = 16;  // Up to 16 textures
        bindings[7].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount                    = 8;
        layoutInfo.pBindings                       = bindings;

        vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout);

        // Create descriptor pool
        VkDescriptorPoolSize poolSizes[4] = {};
        poolSizes[0].type                 = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        poolSizes[0].descriptorCount      = 1;
        poolSizes[1].type                 = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[1].descriptorCount      = 1;
        poolSizes[2].type                 = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[2].descriptorCount      = 5;
        poolSizes[3].type                 = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[3].descriptorCount      = 16;

        VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets                    = 1;
        poolInfo.poolSizeCount              = 4;
        poolInfo.pPoolSizes                 = poolSizes;

        vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool);

        // Allocate descriptor set
        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool              = m_DescriptorPool;
        allocInfo.descriptorSetCount          = 1;
        allocInfo.pSetLayouts                 = &m_DescriptorSetLayout;

        vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet);
    }

    void Renderer::CreateSceneBuffers(const Scene& scene)
    {
        // Calculate total vertices and indices across all meshes (static + dynamic)
        size_t totalVertices = 0;
        size_t totalIndices  = 0;
        for (const auto& mesh : scene.StaticMeshes) {
            totalVertices += mesh.Vertices.size();
            totalIndices += mesh.Indices.size();
        }
        for (const auto& mesh : scene.DynamicMeshes) {
            totalVertices += mesh.Vertices.size();
            totalIndices += mesh.Indices.size();
        }

        // Create vertex buffer
        size_t vertexCount = std::max(totalVertices, (size_t) 1);
        m_VertexBufferSize = sizeof(GPUVertex) * vertexCount;
        m_VertexBuffer     = CreateBuffer(m_VertexBufferSize,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_VertexMemory);

        // Create index buffer
        size_t indexCount = std::max(totalIndices, (size_t) 1);
        m_IndexBufferSize = sizeof(uint32_t) * indexCount;
        m_IndexBuffer     = CreateBuffer(m_IndexBufferSize,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_IndexMemory);

        // Create material buffer
        size_t materialCount = std::max(scene.Materials.size(), (size_t) 1);
        m_MaterialBufferSize = sizeof(GPUMaterial) * materialCount;
        m_MaterialBuffer     = CreateBuffer(m_MaterialBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_MaterialMemory);

        // Create material index buffer (one uint32 per triangle)
        size_t triangleCount      = std::max(totalIndices / 3, (size_t) 1);
        m_MaterialIndexBufferSize = sizeof(uint32_t) * triangleCount;
        m_MaterialIndexBuffer     = CreateBuffer(m_MaterialIndexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_MaterialIndexMemory);

        // Create light buffer
        size_t lightCount = std::max(scene.Lights.size(), (size_t) 1);
        m_LightBufferSize = sizeof(GPULight) * lightCount;
        m_LightBuffer     = CreateBuffer(m_LightBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_LightMemory);
    }

    void Renderer::UpdateSceneData(const Scene& scene)
    {
        // Flatten all meshes (static + dynamic) into single vertex/index buffers
        std::vector<GPUVertex> gpuVertices;
        std::vector<uint32_t> gpuIndices;
        std::vector<uint32_t> materialIndices;  // Material index per triangle
        uint32_t vertexOffset = 0;

        // Process static meshes
        for (const auto& mesh : scene.StaticMeshes) {
            // Copy vertices and apply mesh transform
            for (const auto& vertex : mesh.Vertices) {
                GPUVertex gpuVert{};
                // Transform position
                glm::vec4 transformedPos = mesh.Transform * glm::vec4(vertex.Position, 1.0f);
                gpuVert.position         = glm::vec3(transformedPos);
                // Transform normal (use transpose of inverse for normals, but for uniform scaling this simplifies)
                glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(mesh.Transform)));
                gpuVert.normal         = glm::normalize(normalMatrix * vertex.Normal);
                gpuVert.texCoord       = vertex.TexCoord;
                gpuVertices.push_back(gpuVert);
            }

            // Copy indices with offset
            for (const auto& index : mesh.Indices) { gpuIndices.push_back(index + vertexOffset); }

            // Store material index for each triangle in this mesh
            uint32_t triangleCount = static_cast<uint32_t>(mesh.Indices.size() / 3);
            for (uint32_t i = 0; i < triangleCount; i++) {
                materialIndices.push_back(static_cast<uint32_t>(mesh.MaterialIndex));
            }

            vertexOffset += static_cast<uint32_t>(mesh.Vertices.size());
        }

        // Process dynamic meshes
        for (const auto& mesh : scene.DynamicMeshes) {
            // Copy vertices and apply mesh transform
            for (const auto& vertex : mesh.Vertices) {
                GPUVertex gpuVert{};
                // Transform position
                glm::vec4 transformedPos = mesh.Transform * glm::vec4(vertex.Position, 1.0f);
                gpuVert.position         = glm::vec3(transformedPos);
                // Transform normal (use transpose of inverse for normals, but for uniform scaling this simplifies)
                glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(mesh.Transform)));
                gpuVert.normal         = glm::normalize(normalMatrix * vertex.Normal);
                gpuVert.texCoord       = vertex.TexCoord;
                gpuVertices.push_back(gpuVert);
            }

            // Copy indices with offset
            for (const auto& index : mesh.Indices) { gpuIndices.push_back(index + vertexOffset); }

            // Store material index for each triangle in this mesh
            uint32_t triangleCount = static_cast<uint32_t>(mesh.Indices.size() / 3);
            for (uint32_t i = 0; i < triangleCount; i++) {
                materialIndices.push_back(static_cast<uint32_t>(mesh.MaterialIndex));
            }

            vertexOffset += static_cast<uint32_t>(mesh.Vertices.size());
        }

        // Upload vertex data to GPU
        void* data;
        if (!gpuVertices.empty()) {
            vkMapMemory(m_Device, m_VertexMemory, 0, sizeof(GPUVertex) * gpuVertices.size(), 0, &data);
            memcpy(data, gpuVertices.data(), sizeof(GPUVertex) * gpuVertices.size());
            vkUnmapMemory(m_Device, m_VertexMemory);
        }

        // Upload index data to GPU
        if (!gpuIndices.empty()) {
            vkMapMemory(m_Device, m_IndexMemory, 0, sizeof(uint32_t) * gpuIndices.size(), 0, &data);
            memcpy(data, gpuIndices.data(), sizeof(uint32_t) * gpuIndices.size());
            vkUnmapMemory(m_Device, m_IndexMemory);
        }

        // Upload material data
        if (!scene.Materials.empty()) {
            std::vector<GPUMaterial> gpuMaterials(scene.Materials.size());
            for (size_t i = 0; i < scene.Materials.size(); ++i) {
                gpuMaterials[i].albedo    = scene.Materials[i].Albedo;
                gpuMaterials[i].shininess = scene.Materials[i].Shininess;
                gpuMaterials[i].specular  = scene.Materials[i].Specular;
                gpuMaterials[i].tiling    = scene.Materials[i].Tiling;

                // Sync texture index
                gpuMaterials[i].textureIndex = -1;
                if (!scene.Materials[i].TextureFilename.empty()) {
                    // Try to finding texture in cache
                    auto tex = LoadOrGetTexture(scene.Materials[i].TextureFilename);
                    if (tex) {
                        // Find index in cache (order is not guaranteed, but we'll collect all textures)
                    }
                }
            }

            vkMapMemory(m_Device, m_MaterialMemory, 0, sizeof(GPUMaterial) * gpuMaterials.size(), 0, &data);
            memcpy(data, gpuMaterials.data(), sizeof(GPUMaterial) * gpuMaterials.size());
            vkUnmapMemory(m_Device, m_MaterialMemory);
        }

        // Upload material index data (one per triangle)
        if (!materialIndices.empty()) {
            vkMapMemory(m_Device, m_MaterialIndexMemory, 0, sizeof(uint32_t) * materialIndices.size(), 0, &data);
            memcpy(data, materialIndices.data(), sizeof(uint32_t) * materialIndices.size());
            vkUnmapMemory(m_Device, m_MaterialIndexMemory);
        }

        // Upload light data
        if (!scene.Lights.empty()) {
            std::vector<GPULight> gpuLights(scene.Lights.size());
            for (size_t i = 0; i < scene.Lights.size(); i++) {
                gpuLights[i].position  = scene.Lights[i].Position;
                gpuLights[i].intensity = scene.Lights[i].Intensity;
                gpuLights[i].color     = scene.Lights[i].Color;
                gpuLights[i].type      = scene.Lights[i].Type;
                gpuLights[i].direction = scene.Lights[i].Direction;
                gpuLights[i].radius    = scene.Lights[i].Radius;
            }

            vkMapMemory(m_Device, m_LightMemory, 0, sizeof(GPULight) * gpuLights.size(), 0, &data);
            memcpy(data, gpuLights.data(), sizeof(GPULight) * gpuLights.size());
            vkUnmapMemory(m_Device, m_LightMemory);
        }

        // Rebuild acceleration structure with all meshes (static + dynamic)
        std::vector<Mesh> allMeshes;
        allMeshes.insert(allMeshes.end(), scene.StaticMeshes.begin(), scene.StaticMeshes.end());
        allMeshes.insert(allMeshes.end(), scene.DynamicMeshes.begin(), scene.DynamicMeshes.end());
        m_AccelerationStructure->Rebuild(allMeshes, m_VertexBuffer, m_IndexBuffer);

        // Update descriptor sets
        VkWriteDescriptorSetAccelerationStructureKHR asInfo
                = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
        asInfo.accelerationStructureCount = 1;
        VkAccelerationStructureKHR tlas   = m_AccelerationStructure->GetTLAS();
        asInfo.pAccelerationStructures    = &tlas;

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageView             = m_FinalImage->GetVkImageView();
        imageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorBufferInfo vertexBufferInfo = {};
        vertexBufferInfo.buffer                 = m_VertexBuffer;
        vertexBufferInfo.offset                 = 0;
        vertexBufferInfo.range                  = m_VertexBufferSize > 0 ? m_VertexBufferSize : 16;

        VkDescriptorBufferInfo indexBufferInfo = {};
        indexBufferInfo.buffer                 = m_IndexBuffer;
        indexBufferInfo.offset                 = 0;
        indexBufferInfo.range                  = m_IndexBufferSize > 0 ? m_IndexBufferSize : 16;

        VkDescriptorBufferInfo materialBufferInfo = {};
        materialBufferInfo.buffer                 = m_MaterialBuffer;
        materialBufferInfo.offset                 = 0;
        materialBufferInfo.range                  = m_MaterialBufferSize > 0 ? m_MaterialBufferSize : 16;

        VkDescriptorBufferInfo materialIndexBufferInfo = {};
        materialIndexBufferInfo.buffer                 = m_MaterialIndexBuffer;
        materialIndexBufferInfo.offset                 = 0;
        materialIndexBufferInfo.range                  = m_MaterialIndexBufferSize > 0 ? m_MaterialIndexBufferSize : 16;

        VkDescriptorBufferInfo lightBufferInfo = {};
        lightBufferInfo.buffer                 = m_LightBuffer;
        lightBufferInfo.offset                 = 0;
        lightBufferInfo.range                  = m_LightBufferSize > 0 ? m_LightBufferSize : 16;

        // Collect all textures from the scene materials
        std::vector<std::shared_ptr<Walnut::Image>> allTextures;
        std::unordered_map<std::string, int> textureToIndex;
        for (const auto& mat : scene.Materials) {
            if (!mat.TextureFilename.empty() && textureToIndex.find(mat.TextureFilename) == textureToIndex.end()) {
                auto tex = LoadOrGetTexture(mat.TextureFilename);
                if (tex) {
                    textureToIndex[mat.TextureFilename] = (int) allTextures.size();
                    allTextures.push_back(tex);
                }
            }
        }

        // Update material buffer again with the correct texture indices
        if (!scene.Materials.empty()) {
            std::vector<GPUMaterial> gpuMaterials(scene.Materials.size());
            for (size_t i = 0; i < scene.Materials.size(); i++) {
                gpuMaterials[i].albedo    = scene.Materials[i].Albedo;
                gpuMaterials[i].shininess = scene.Materials[i].Shininess;
                gpuMaterials[i].specular  = scene.Materials[i].Specular;
                gpuMaterials[i].tiling    = scene.Materials[i].Tiling;

                auto it                      = textureToIndex.find(scene.Materials[i].TextureFilename);
                gpuMaterials[i].textureIndex = (it != textureToIndex.end()) ? it->second : -1;
            }
            void* data;
            vkMapMemory(m_Device, m_MaterialMemory, 0, sizeof(GPUMaterial) * gpuMaterials.size(), 0, &data);
            memcpy(data, gpuMaterials.data(), sizeof(GPUMaterial) * gpuMaterials.size());
            vkUnmapMemory(m_Device, m_MaterialMemory);
        }

        std::vector<VkDescriptorImageInfo> textureInfos(16);
        for (size_t i = 0; i < 16; i++) {
            if (i < allTextures.size()) {
                textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                textureInfos[i].imageView   = allTextures[i]->GetVkImageView();
                textureInfos[i].sampler     = allTextures[i]->GetVkSampler();
            }
            else {
                // Point to final image as a fallback (valid object for Vulkan)
                textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                textureInfos[i].imageView   = m_FinalImage->GetVkImageView();
                textureInfos[i].sampler     = m_FinalImage->GetVkSampler();
            }
        }

        VkWriteDescriptorSet writes[8] = {};

        // Acceleration structure
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].pNext           = &asInfo;
        writes[0].dstSet          = m_DescriptorSet;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        // Storage image
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_DescriptorSet;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo      = &imageInfo;

        // Vertex buffer
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_DescriptorSet;
        writes[2].dstBinding      = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo     = &vertexBufferInfo;

        // Index buffer
        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_DescriptorSet;
        writes[3].dstBinding      = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo     = &indexBufferInfo;

        // Material buffer
        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_DescriptorSet;
        writes[4].dstBinding      = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo     = &materialBufferInfo;

        // Material index buffer
        writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = m_DescriptorSet;
        writes[5].dstBinding      = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].pBufferInfo     = &materialIndexBufferInfo;

        // Light buffer
        writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet          = m_DescriptorSet;
        writes[6].dstBinding      = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].pBufferInfo     = &lightBufferInfo;

        // Textures
        writes[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet          = m_DescriptorSet;
        writes[7].dstBinding      = 7;
        writes[7].descriptorCount = 16;
        writes[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].pImageInfo      = textureInfos.data();

        vkUpdateDescriptorSets(m_Device, 8, writes, 0, nullptr);
    }

    auto Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
            VkDeviceMemory& bufferMemory) const -> VkBuffer
    {
        if (size == 0) size = 16;  // Minimum size to avoid Vulkan errors

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = size;
        bufferInfo.usage              = usage;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer;
        vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

        // Find memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(Walnut::Application::GetPhysicalDevice(), &memProperties);

        uint32_t memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i))
                    && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                memoryTypeIndex = i;
                break;
            }
        }

        VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        allocFlagsInfo.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.pNext           = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &allocFlagsInfo : nullptr;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory);
        vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);

        return buffer;
    }

    void Renderer::PreloadTextures(const std::vector<std::string>& textureFilenames)
    {
        for (const auto& textureFilename : textureFilenames) { LoadOrGetTexture(textureFilename); }
    }

    auto Renderer::LoadOrGetTexture(const std::string& filename) -> std::shared_ptr<Walnut::Image>
    {
        auto filepath = Vlkrt::TEXTURES_DIR + filename;

        // Check if texture already cached
        auto it = m_TextureCache.find(filename);
        if (it != m_TextureCache.end()) { return it->second; }

        // Try to load texture from disk
        std::shared_ptr<Walnut::Image> texture;

        try {
            auto newImg = std::make_shared<Walnut::Image>(filepath);
            if (newImg->GetWidth() > 0) {
                texture                  = newImg;
                m_TextureCache[filename] = texture;
                WL_INFO_TAG("Renderer", "Loaded texture: {}", filepath);
            }
        }
        catch (const std::exception& e) {
            WL_WARN_TAG("Renderer", "Failed to load texture '{}': {}", filepath, e.what());
        }

        if (!texture) { WL_WARN_TAG("Renderer", "Failed to load texture '{}'.", filepath); }

        return texture;
    }
}  // namespace Vlkrt