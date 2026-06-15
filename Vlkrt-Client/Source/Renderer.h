#pragma once

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include "Walnut/Image.h"
#include "AccelerationStructure.h"
#include "NRDDenoiser.h"

#include <memory>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>


namespace Vlkrt
{
    class Camera;
    struct Scene;
    class FSRUpscaler;

    /// <summary>
    /// GPU-aligned vertex structure.
    /// </summary>
    struct GPUVertex
    {
        glm::vec3 position;  // 0
        float _pad1;         // 12
        glm::vec3 normal;    // 16
        float _pad2;         // 28
        glm::vec2 texCoord;  // 32
        glm::vec2 _pad3;     // 40
        // 48
    };
    static_assert(sizeof(GPUVertex) == 48, "GPUVertex size mismatch");

    /// <summary>
    /// GPU-aligned light structure.
    /// </summary>
    struct GPULight
    {
        glm::vec3 position;   // 0
        float intensity;      // 12
        glm::vec3 emission;   // 16
        float size;           // 28
        glm::vec3 direction;  // 32
        uint32_t type;        // 44
        // 48
    };
    static_assert(sizeof(GPULight) == 48, "GPULight size mismatch");

    /// <summary>
    /// Disney-BSDF material on the GPU.
    /// </summary>
    struct GPUPBRMaterial
    {
        glm::vec3 albedo;            // 0
        int32_t textureIndex;        // 12
        glm::vec3 emission;          // 16
        float tiling;                // 28
        glm::vec3 extinction;        // 32
        uint32_t materialIndex;      // 44
        float stepScale;             // 48
        float sheen;                 // 52
        float sheenTint;             // 56
        float clearcoat;             // 60
        float clearcoatGloss;        // 64
        float roughness;             // 68
        float subsurface;            // 72
        float anisotropic;           // 76
        float metallic;              // 80
        float specularTint;          // 84
        float specularTransmission;  // 88
        float eta;                   // 92
        float atDistance;            // 96
        int32_t lightIndex;          // 100
        float _pad1;                 // 104
        float _pad2;                 // 108
        // 112
    };
    static_assert(sizeof(GPUPBRMaterial) == 112, "GPUPBRMaterial size mismatch");

    /// <summary>
    /// AABB primitive transform pair.
    /// </summary>
    struct AABBTransform
    {
        glm::mat4 localSpaceToBottomLevelAS;  // 0
        glm::mat4 bottomLevelASToLocalSpace;  // 64
        // 128
    };

    /// <summary>
    /// Scene UBO sent to shaders every frame.
    /// </summary>
    struct SceneUBOData
    {
        glm::mat4 projectionToWorld;       // 0
        glm::mat4 worldToClip;             // 64
        glm::mat4 worldToClipPrev;         // 128
        glm::vec4 cameraPosition;          // 192
        glm::vec4 backgroundColor;         // 208
        uint32_t numLights;                // 224
        float elapsedTime;                 // 228
        uint32_t elapsedTicks;             // 232
        uint32_t raytracingType;           // 236
        uint32_t importanceSamplingType;   // 240
        uint32_t maxRecursionDepth;        // 244
        uint32_t maxShadowRecursionDepth;  // 248
        uint32_t pathSqrtSamplesPerPixel;  // 252
        uint32_t pathFrameCacheIndex;      // 256
        uint32_t applyJitter;              // 260
        uint32_t onlyOneLightSample;       // 264
        uint32_t russianRouletteDepth;     // 268
        uint32_t anisotropicBSDF;          // 272
        uint32_t sceneIndex;               // 276
        float cameraForward[3];            // 280
        uint32_t nrdDebugViewMode;         // 292
        float _pad[2];                     // 296
        // 304
    };
    static_assert(sizeof(SceneUBOData) == 304, "SceneUBOData size mismatch");

    /// <summary>
    /// Struct to hold render pass statistics for performance monitoring.
    /// </summary>
    struct RenderPassStats
    {
        float SceneSetupMs{ 0.0f };
        float UBOUploadMs{ 0.0f };
        float RayTraceRecordMs{ 0.0f };
        float NRDRecordMs{ 0.0f };
        float CommandSubmitMs{ 0.0f };
        float FrameTotalMs{ 0.0f };
        bool NRDEnabled{ false };
        bool NRDOperational{ false };
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };
        float EstimatedGraphicsMemoryMB{ 0.0f };
    };

    /// <summary>
    /// Runtime quality metrics comparing denoised output against raw (non-denoised) output.
    /// </summary>
    struct DenoiseComparisonMetrics
    {
        bool Valid{ false };
        uint32_t SampleCount{ 0 };
        float LumaMSE{ 0.0f };
        float LumaRMSE{ 0.0f };
        float LumaPSNR{ 0.0f };
        float LumaMeanAbsDiff{ 0.0f };
    };

    /// <summary>
    /// Class that manages the Vulkan ray tracing pipeline, scene data, and rendering process.
    /// </summary>
    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        void OnResize(uint32_t width, uint32_t height);
        void Render(const Scene& scene, const Camera& camera);

        void InvalidateScene() { m_SceneValid = false; }
        void InvalidateSceneStructure()
        {
            m_SceneValid          = false;
            m_LastProceduralCount = UINT32_MAX;
        }
        void ResetAccumulation()
        {
            m_SceneValid      = false;
            m_FrameIndex      = 0;
            m_AccumFirstFrame = true;
        }
        auto GetAccumulatedFrameCount() const -> uint32_t { return m_FrameIndex; }
        auto GetNRDStatus() const -> const char* { return m_NRDDenoiser.GetStatus(); }
        auto IsNRDOperational() const -> bool { return m_NRDDenoiser.IsOperational(); }

        void MarkDirtyMeshes(const std::vector<uint32_t>& meshIndices)
        { m_DirtyMeshIndices.insert(m_DirtyMeshIndices.end(), meshIndices.begin(), meshIndices.end()); }

        void MarkDirtyLights(const std::vector<uint32_t>& lightIndices)
        { m_DirtyLightIndices.insert(m_DirtyLightIndices.end(), lightIndices.begin(), lightIndices.end()); }

        auto GetFinalImage() const -> std::shared_ptr<Walnut::Image> { return m_FinalImage; }
        auto GetGuideNormalRoughness() const -> std::shared_ptr<Walnut::Image> { return m_GuideNormalRoughness; }
        auto GetGuideViewZ() const -> std::shared_ptr<Walnut::Image> { return m_GuideViewZ; }
        auto GetGuideMotionVectors() const -> std::shared_ptr<Walnut::Image> { return m_GuideMotionVectors; }
        auto GetGuideDiffRadianceHitDist() const -> std::shared_ptr<Walnut::Image>
        { return m_GuideDiffRadianceHitDist; }
        auto GetGuideSpecRadianceHitDist() const -> std::shared_ptr<Walnut::Image>
        { return m_GuideSpecRadianceHitDist; }
        auto GetGuideEmission() const -> std::shared_ptr<Walnut::Image> { return m_GuideEmission; }
        auto GetGuideDepth() const -> std::shared_ptr<Walnut::Image> { return m_GuideDepth; }
        auto GetUpscaledImage() const -> std::shared_ptr<Walnut::Image> { return m_FinalImageUpscaled; }
        auto GetLastPassStats() const -> const RenderPassStats& { return m_LastPassStats; }
        auto GetDenoiseComparisonMetrics() const -> const DenoiseComparisonMetrics& { return m_LastDenoiseMetrics; }

        void OnFSRSettingsChanged(bool enabled, uint32_t qualityMode, float sharpness);

        void PreloadTextures(const std::vector<std::string>& textureFilenames);

    private:
        auto LoadOrGetTexture(const std::string& filename) -> std::shared_ptr<Walnut::Image>;

        void CreateRayTracingPipeline();
        void DestroyPipelineObjects();
        void CreateShaderBindingTable(const Scene& scene);
        void CreateDescriptorSets();
        void CreateSceneBuffers(const Scene& scene);
        void UpdateSceneData(const Scene& scene);
        void UpdateSceneUBO(const Scene& scene, const Camera& camera);
        auto CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                VkDeviceMemory& bufferMemory) const -> VkBuffer;

    private:
        std::shared_ptr<Walnut::Image> m_FinalImage;

        const Scene* m_ActiveScene{ nullptr };
        const Camera* m_ActiveCamera{ nullptr };

        // Ray tracing pipeline
        VkPipeline m_RTPipeline{ VK_NULL_HANDLE };
        VkPipelineLayout m_RTPipelineLayout{ VK_NULL_HANDLE };
        VkPipelineLayout m_ComputePipelineLayout{ VK_NULL_HANDLE };

        // Shader modules
        VkShaderModule m_RaygenShader{ VK_NULL_HANDLE };
        VkShaderModule m_RaygenTemporalShader{ VK_NULL_HANDLE };
        VkShaderModule m_MissShader{ VK_NULL_HANDLE };
        VkShaderModule m_ShadowMissShader{ VK_NULL_HANDLE };
        VkShaderModule m_ClosestHitShader{ VK_NULL_HANDLE };
        VkShaderModule m_ClosestHitAABBShader{ VK_NULL_HANDLE };
        VkShaderModule m_IntersectAnalyticShader{ VK_NULL_HANDLE };
        VkShaderModule m_IntersectSDFShader{ VK_NULL_HANDLE };
        VkShaderModule m_ComposeDenoisedShader{ VK_NULL_HANDLE };

        VkPipeline m_ComposeDenoisedPipeline{ VK_NULL_HANDLE };

        // Shader binding table
        VkBuffer m_SBTBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_SBTMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_SBTBufferSize{ 0 };
        VkStridedDeviceAddressRegionKHR m_RaygenRegion{};
        VkStridedDeviceAddressRegionKHR m_MissRegion{};
        VkStridedDeviceAddressRegionKHR m_HitRegion{};
        VkStridedDeviceAddressRegionKHR m_CallableRegion{};

        // Descriptor sets
        VkDescriptorSetLayout m_DescriptorSetLayout{ VK_NULL_HANDLE };
        VkDescriptorPool m_DescriptorPool{ VK_NULL_HANDLE };
        VkDescriptorSet m_DescriptorSet{ VK_NULL_HANDLE };

        // Scene buffers (triangle geometry)
        VkBuffer m_VertexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_VertexMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_VertexBufferSize{ 0 };

        VkBuffer m_PrevVertexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_PrevVertexMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_PrevVertexBufferSize{ 0 };

        VkBuffer m_IndexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_IndexMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_IndexBufferSize{ 0 };

        VkBuffer m_MaterialBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_MaterialMemory{ VK_NULL_HANDLE };
        size_t m_MaterialBufferSize{ 0 };

        // Material index buffer (maps triangle ID to material index)
        VkBuffer m_MaterialIndexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_MaterialIndexMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_MaterialIndexBufferSize{ 0 };

        // Light buffer
        VkBuffer m_LightBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_LightMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_LightBufferSize{ 0 };

        // AABB procedural geometry buffers
        VkBuffer m_AABBTransformBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_AABBTransformMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_AABBTransformBufferSize{ 0 };

        VkBuffer m_PrevAABBTransformBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_PrevAABBTransformMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_PrevAABBTransformBufferSize{ 0 };

        VkBuffer m_AABBMaterialBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_AABBMaterialMemory{ VK_NULL_HANDLE };
        VkDeviceSize m_AABBMaterialBufferSize{ 0 };

        // Scene UBO
        VkBuffer m_SceneUBOBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_SceneUBOMemory{ VK_NULL_HANDLE };

        // Denoise quality metric accumulation buffer (updated by compose compute shader)
        VkBuffer m_QualityMetricsBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_QualityMetricsMemory{ VK_NULL_HANDLE };

        // Temporal accumulation image
        std::shared_ptr<Walnut::Image> m_AccumImage;

        // NRD
        NRDDenoiser m_NRDDenoiser;
        std::shared_ptr<Walnut::Image> m_GuideNormalRoughness;  // RGBA32F: packed normal+roughness+material ID
        std::shared_ptr<Walnut::Image> m_GuideViewZ;  // RGBA16F: view depth + material data (narrower precision)
        std::shared_ptr<Walnut::Image> m_GuideMotionVectors;  // RGBA16F: motion vectors + metallic (narrower precision)
        std::shared_ptr<Walnut::Image> m_GuideDiffRadianceHitDist;  // RGBA16F: diffuse radiance + hit distance
        std::shared_ptr<Walnut::Image> m_GuideSpecRadianceHitDist;  // RGBA16F: specular radiance + hit distance
        std::shared_ptr<Walnut::Image> m_GuideEmission;             // RGBA16F: direct emission
        std::shared_ptr<Walnut::Image> m_GuideDepth;                // R32F: NDC depth (scalar only)

        // FSR
        std::unique_ptr<FSRUpscaler> m_FSRUpscaler;
        std::shared_ptr<Walnut::Image> m_FinalImageUpscaled;
        uint32_t m_RenderWidth{ 0 };
        uint32_t m_RenderHeight{ 0 };
        uint32_t m_DisplayWidth{ 0 };
        uint32_t m_DisplayHeight{ 0 };
        bool m_FSREnabled{ false };
        uint32_t m_FSRQuality{ 1 };
        float m_FSRSharpness{ 0.0f };

        // Frame counter for temporal accumulation (resets on scene change)
        uint32_t m_FrameIndex{ 0 };
        // Global tick counter for PRNG seeds (never resets)
        uint32_t m_GlobalTick{ 0 };

        // Dirty tracking for incremental GPU updates
        std::vector<uint32_t> m_DirtyMeshIndices;
        std::vector<uint32_t> m_DirtyLightIndices;

        std::unique_ptr<AccelerationStructure> m_AccelerationStructure;

        VkDevice m_Device{ VK_NULL_HANDLE };
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_RTPipelineProperties{};

        // Track scene changes to avoid unnecessary AS rebuilds
        size_t m_LastMeshCount{ 0 };
        size_t m_LastVertexCount{ 0 };
        size_t m_LastIndexCount{ 0 };
        size_t m_LastMaterialCount{ 0 };
        size_t m_LastLightCount{ 0 };
        uint32_t m_LastProceduralCount{ UINT32_MAX };  // UINT32_MAX forces initial creation

        // Cached scene metrics
        size_t m_CachedTotalMeshCount{ 0 };
        size_t m_CachedTotalVertices{ 0 };
        size_t m_CachedTotalIndices{ 0 };
        bool m_SceneValid{ false };
        bool m_FirstFrame{ true };
        bool m_AccumFirstFrame{ true };
        bool m_GuidesFirstFrame{ true };
        bool m_GuidesInReadOnly{ false };
        glm::mat4 m_LastCameraView{ glm::mat4(0.0f) };
        glm::mat4 m_LastCameraProjection{ glm::mat4(0.0f) };
        glm::vec2 m_LastCameraJitter{ 0.0f, 0.0f };
        glm::vec2 m_PrevCameraJitter{ 0.0f, 0.0f };
        float m_ElapsedTime{ 0.0f };
        RenderPassStats m_LastPassStats{};
        DenoiseComparisonMetrics m_LastDenoiseMetrics{};

        std::vector<GPUVertex> m_PreviousFrameVertices;
        std::vector<AABBTransform> m_PreviousFrameAABBTransforms;

        std::unordered_map<std::string, std::shared_ptr<Walnut::Image>> m_TextureCache;
    };
}  // namespace Vlkrt
