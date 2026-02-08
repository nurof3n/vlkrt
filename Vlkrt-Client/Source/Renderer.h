#pragma once

#include "Walnut/Image.h"
#include "AccelerationStructure.h"

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>


namespace Vlkrt
{
    class Camera;
    struct Scene;

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        void OnResize(uint32_t width, uint32_t height);
        void Render(const Scene& scene, const Camera& camera);

        // Call this when spheres change position (for multiplayer updates)
        void InvalidateScene() { m_SceneValid = false; }

        std::shared_ptr<Walnut::Image> GetFinalImage() const
        {
            return m_FinalImage;
        }

    private:
        void CreateRayTracingPipeline();
        void CreateShaderBindingTable();
        void CreateDescriptorSets();
        void CreateSceneBuffers(const Scene& scene);
        void UpdateSceneData(const Scene& scene);
        VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory);

    private:
        std::shared_ptr<Walnut::Image> m_FinalImage;

        const Scene*  m_ActiveScene  = nullptr;
        const Camera* m_ActiveCamera = nullptr;

        // Ray tracing pipeline
        VkPipeline m_RTPipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_RTPipelineLayout = VK_NULL_HANDLE;

        // Shader modules
        VkShaderModule m_RaygenShader = VK_NULL_HANDLE;
        VkShaderModule m_MissShader = VK_NULL_HANDLE;
        VkShaderModule m_ClosestHitShader = VK_NULL_HANDLE;
        VkShaderModule m_IntersectionShader = VK_NULL_HANDLE;

        // Shader binding table
        VkBuffer m_SBTBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_SBTMemory = VK_NULL_HANDLE;
        VkStridedDeviceAddressRegionKHR m_RaygenRegion = {};
        VkStridedDeviceAddressRegionKHR m_MissRegion = {};
        VkStridedDeviceAddressRegionKHR m_HitRegion = {};
        VkStridedDeviceAddressRegionKHR m_CallableRegion = {};

        // Descriptor sets
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

        // Scene buffers
        VkBuffer m_SphereBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_SphereMemory = VK_NULL_HANDLE;
        size_t m_SphereBufferSize = 0;

        VkBuffer m_MaterialBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_MaterialMemory = VK_NULL_HANDLE;
        size_t m_MaterialBufferSize = 0;

        // Acceleration structure
        std::unique_ptr<AccelerationStructure> m_AccelerationStructure;

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_RTPipelineProperties = {};

        // Track scene changes to avoid unnecessary AS rebuilds
        size_t m_LastSphereCount = 0;
        size_t m_LastMaterialCount = 0;
        bool m_SceneValid = false;
        bool m_FirstFrame = true;
    };
}  // namespace Vlkrt
