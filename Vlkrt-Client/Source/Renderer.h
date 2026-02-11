#pragma once

#include "Walnut/Image.h"
#include "AccelerationStructure.h"

#include <memory>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>


namespace Vlkrt
{
    class Camera;
    struct Scene;

    // GPU-aligned vertex structure
    struct GPUVertex
    {
        glm::vec3 position;
        float _pad1;
        glm::vec3 normal;
        float _pad2;
        glm::vec2 texCoord;
        glm::vec2 _pad3;
    };

    // GPU-aligned light structure
    struct GPULight
    {
        glm::vec3 position;
        float intensity;
        glm::vec3 color;
        float type;  // 0=Directional, 1=Point
        glm::vec3 direction;
        float radius;
    };

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        void OnResize(uint32_t width, uint32_t height);
        void Render(const Scene& scene, const Camera& camera);

        void InvalidateScene() { m_SceneValid = false; }

        void MarkDirtyMeshes(const std::vector<uint32_t>& meshIndices)
        {
            m_DirtyMeshIndices.insert(m_DirtyMeshIndices.end(), meshIndices.begin(), meshIndices.end());
        }

        void MarkDirtyLights(const std::vector<uint32_t>& lightIndices)
        {
            m_DirtyLightIndices.insert(m_DirtyLightIndices.end(), lightIndices.begin(), lightIndices.end());
        }

        std::shared_ptr<Walnut::Image> GetFinalImage() const { return m_FinalImage; }

        void PreloadTextures(const std::vector<std::string>& textureFilenames);

    private:
        std::shared_ptr<Walnut::Image> LoadOrGetTexture(const std::string& filename);

        void CreateRayTracingPipeline();
        void CreateShaderBindingTable();
        void CreateDescriptorSets();
        void CreateSceneBuffers(const Scene& scene);
        void UpdateSceneData(const Scene& scene);

        VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                VkDeviceMemory& bufferMemory);

    private:
        std::shared_ptr<Walnut::Image> m_FinalImage;

        const Scene* m_ActiveScene   = nullptr;
        const Camera* m_ActiveCamera = nullptr;

        // Ray tracing pipeline
        VkPipeline m_RTPipeline             = VK_NULL_HANDLE;
        VkPipelineLayout m_RTPipelineLayout = VK_NULL_HANDLE;

        // Shader modules
        VkShaderModule m_RaygenShader     = VK_NULL_HANDLE;
        VkShaderModule m_MissShader       = VK_NULL_HANDLE;
        VkShaderModule m_ClosestHitShader = VK_NULL_HANDLE;
        // Removed: VkShaderModule m_IntersectionShader (no longer needed for triangle geometry)

        // Shader binding table
        VkBuffer m_SBTBuffer                             = VK_NULL_HANDLE;
        VkDeviceMemory m_SBTMemory                       = VK_NULL_HANDLE;
        VkStridedDeviceAddressRegionKHR m_RaygenRegion   = {};
        VkStridedDeviceAddressRegionKHR m_MissRegion     = {};
        VkStridedDeviceAddressRegionKHR m_HitRegion      = {};
        VkStridedDeviceAddressRegionKHR m_CallableRegion = {};

        // Descriptor sets
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool           = VK_NULL_HANDLE;
        VkDescriptorSet m_DescriptorSet             = VK_NULL_HANDLE;

        // Scene buffers
        VkBuffer m_VertexBuffer         = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexMemory   = VK_NULL_HANDLE;
        VkDeviceSize m_VertexBufferSize = 0;

        VkBuffer m_IndexBuffer         = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexMemory   = VK_NULL_HANDLE;
        VkDeviceSize m_IndexBufferSize = 0;

        VkBuffer m_MaterialBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_MaterialMemory = VK_NULL_HANDLE;
        size_t m_MaterialBufferSize     = 0;

        // Material index buffer (maps triangle ID to material index)
        VkBuffer m_MaterialIndexBuffer         = VK_NULL_HANDLE;
        VkDeviceMemory m_MaterialIndexMemory   = VK_NULL_HANDLE;
        VkDeviceSize m_MaterialIndexBufferSize = 0;

        // Light buffer
        VkBuffer m_LightBuffer         = VK_NULL_HANDLE;
        VkDeviceMemory m_LightMemory   = VK_NULL_HANDLE;
        VkDeviceSize m_LightBufferSize = 0;

        // Dirty tracking for incremental GPU updates
        std::vector<uint32_t> m_DirtyMeshIndices;   // Mesh indices that changed
        std::vector<uint32_t> m_DirtyLightIndices;  // Light indices that changed

        // Acceleration structure
        std::unique_ptr<AccelerationStructure> m_AccelerationStructure;

        VkDevice m_Device                                                      = VK_NULL_HANDLE;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_RTPipelineProperties = {};

        // Track scene changes to avoid unnecessary AS rebuilds
        size_t m_LastMeshCount     = 0;
        size_t m_LastVertexCount   = 0;
        size_t m_LastIndexCount    = 0;
        size_t m_LastMaterialCount = 0;
        size_t m_LastLightCount    = 0;
        bool m_SceneValid          = false;
        bool m_FirstFrame          = true;

        // Texture loading with caching (loaded at startup, not during runtime)
        std::unordered_map<std::string, std::shared_ptr<Walnut::Image>> m_TextureCache;
    };
}  // namespace Vlkrt
