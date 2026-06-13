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

    // GPU-aligned vertex structure (48 bytes, matches GLSL GPUVertex)
    struct GPUVertex
    {
        glm::vec3 position;
        float _pad1;
        glm::vec3 normal;
        float _pad2;
        glm::vec2 texCoord;
        glm::vec2 _pad3;
    };

    // GPU-aligned light structure (48 bytes, matches GLSL GPULight std430)
    struct GPULight
    {
        glm::vec3 position;    // offset  0
        float     intensity;   // offset 12
        glm::vec3 emission;    // offset 16  (was color)
        float     size;        // offset 28  (was radius)
        glm::vec3 direction;   // offset 32
        uint32_t  type;        // offset 44  [0=Square, 1=Directional]
    };

    // Disney-BSDF material on the GPU (112 bytes, matches GLSL GPUPBRMaterial std430)
    struct GPUPBRMaterial
    {
        glm::vec3 albedo;                //   0
        int32_t   textureIndex;          //  12  [-1 = no texture]
        glm::vec3 emission;              //  16
        float     tiling;                //  28
        glm::vec3 extinction;            //  32
        uint32_t  materialIndex;         //  44  [0 = floor/ground]
        float     stepScale;             //  48
        float     sheen;                 //  52
        float     sheenTint;             //  56
        float     clearcoat;             //  60
        float     clearcoatGloss;        //  64
        float     roughness;             //  68
        float     subsurface;            //  72
        float     anisotropic;           //  76
        float     metallic;              //  80
        float     specularTint;          //  84
        float     specularTransmission;  //  88
        float     eta;                   //  92
        float     atDistance;            //  96
        int32_t   lightIndex;            // 100  [>=0 if light mesh]
        float     _pad1;                 // 104
        float     _pad2;                 // 108
        //                              // 112 total
    };

    // AABB primitive transform pair (128 bytes, matches GLSL AABBTransform std430)
    struct AABBTransform
    {
        glm::mat4 localSpaceToBottomLevelAS;  // offset  0  (local → BLAS)
        glm::mat4 bottomLevelASToLocalSpace;  // offset 64  (BLAS → local)
    };

    // Scene UBO sent to shaders every frame (304 bytes, matches Slang SceneUBO layout)
    struct SceneUBOData
    {
        glm::mat4  projectionToWorld;          //   0 (64)
        glm::mat4  worldToClip;                //  64 (64)
        glm::mat4  worldToClipPrev;            // 128 (64)
        glm::vec4  cameraPosition;             // 192 (16)
        glm::vec4  backgroundColor;            // 208 (16)
        uint32_t   numLights;                  // 224
        float      elapsedTime;                // 228
        uint32_t   elapsedTicks;               // 232
        uint32_t   raytracingType;             // 236
        uint32_t   importanceSamplingType;     // 240
        uint32_t   maxRecursionDepth;          // 244
        uint32_t   maxShadowRecursionDepth;    // 248
        uint32_t   pathSqrtSamplesPerPixel;    // 252
        uint32_t   pathFrameCacheIndex;        // 256
        uint32_t   applyJitter;                // 260
        uint32_t   onlyOneLightSample;         // 264
        uint32_t   russianRouletteDepth;       // 268
        uint32_t   anisotropicBSDF;            // 272
        uint32_t   sceneIndex;                 // 276
        float      cameraForward[3];           // 280 (12)
        uint32_t   nrdDebugViewMode;           // 292 (4)
        float      _pad[2];                    // 296 (8)
        //                                     // 304 total
    };
    static_assert(sizeof(SceneUBOData) == 304, "SceneUBOData size mismatch");

    struct RenderPassStats
    {
        float SceneSetupMs{ 0.0f };
        float UBOUploadMs{ 0.0f };
        float RayTraceRecordMs{ 0.0f };
        float NRDRecordMs{ 0.0f };
        float CommandSubmitMs{ 0.0f };
        float FrameTotalMs{ 0.0f };
        bool  NRDEnabled{ false };
        bool  NRDOperational{ false };
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };
    };

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        void OnResize(uint32_t width, uint32_t height);
        void Render(const Scene& scene, const Camera& camera);

        void InvalidateScene() { m_SceneValid = false; }
        void InvalidateSceneStructure() { m_SceneValid = false; m_LastProceduralCount = UINT32_MAX; }
        void ResetAccumulation() { m_SceneValid = false; m_FrameIndex = 0; m_AccumFirstFrame = true; }
        uint32_t GetAccumulatedFrameCount() const { return m_FrameIndex; }
        const char* GetNRDStatus() const { return m_NRDDenoiser.GetStatus(); }
        bool IsNRDOperational() const { return m_NRDDenoiser.IsOperational(); }

        void MarkDirtyMeshes(const std::vector<uint32_t>& meshIndices)
        {
            m_DirtyMeshIndices.insert(m_DirtyMeshIndices.end(), meshIndices.begin(), meshIndices.end());
        }

        void MarkDirtyLights(const std::vector<uint32_t>& lightIndices)
        {
            m_DirtyLightIndices.insert(m_DirtyLightIndices.end(), lightIndices.begin(), lightIndices.end());
        }

        auto GetFinalImage() const -> std::shared_ptr<Walnut::Image> { return m_FinalImage; }
        auto GetGuideNormalRoughness() const -> std::shared_ptr<Walnut::Image> { return m_GuideNormalRoughness; }
        auto GetGuideViewZ() const -> std::shared_ptr<Walnut::Image> { return m_GuideViewZ; }
        auto GetGuideMotionVectors() const -> std::shared_ptr<Walnut::Image> { return m_GuideMotionVectors; }
        auto GetGuideDiffRadianceHitDist() const -> std::shared_ptr<Walnut::Image> { return m_GuideDiffRadianceHitDist; }
        auto GetGuideSpecRadianceHitDist() const -> std::shared_ptr<Walnut::Image> { return m_GuideSpecRadianceHitDist; }
        auto GetGuideEmission() const -> std::shared_ptr<Walnut::Image> { return m_GuideEmission; }
        auto GetGuideDepth() const -> std::shared_ptr<Walnut::Image> { return m_GuideDepth; }
        auto GetUpscaledImage() const -> std::shared_ptr<Walnut::Image> { return m_FinalImageUpscaled; }
        const RenderPassStats& GetLastPassStats() const { return m_LastPassStats; }

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
        VkShaderModule m_RaygenShader          { VK_NULL_HANDLE };
        VkShaderModule m_RaygenTemporalShader  { VK_NULL_HANDLE };
        VkShaderModule m_MissShader            { VK_NULL_HANDLE };
        VkShaderModule m_ShadowMissShader      { VK_NULL_HANDLE };
        VkShaderModule m_ClosestHitShader      { VK_NULL_HANDLE };
        VkShaderModule m_ClosestHitAABBShader  { VK_NULL_HANDLE };
        VkShaderModule m_IntersectAnalyticShader { VK_NULL_HANDLE };
        VkShaderModule m_IntersectSDFShader    { VK_NULL_HANDLE };
        VkShaderModule m_ComposeDenoisedShader { VK_NULL_HANDLE };

        VkPipeline m_ComposeDenoisedPipeline{ VK_NULL_HANDLE };

        // Shader binding table
        VkBuffer m_SBTBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_SBTMemory{ VK_NULL_HANDLE };
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

        // Scene UBO (binding 11 — replaces push constants)
        VkBuffer m_SceneUBOBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory m_SceneUBOMemory{ VK_NULL_HANDLE };

        // Temporal accumulation image (binding 10, rgba32f)
        std::shared_ptr<Walnut::Image> m_AccumImage;

        // NRD guide buffers for full REBLUR denoising
        // These are written by RT shader and read by NRD compute shaders
        std::shared_ptr<Walnut::Image> m_GuideNormalRoughness;     // RGB=normal, A=roughness (RGBA32F)
        std::shared_ptr<Walnut::Image> m_GuideViewZ;               // R=view space depth (RGBA32F)
        std::shared_ptr<Walnut::Image> m_GuideMotionVectors;       // RG=motion (RGBA32F)
        std::shared_ptr<Walnut::Image> m_GuideDiffRadianceHitDist; // RGB=diffuse, A=hit distance (RGBA32F)
        std::shared_ptr<Walnut::Image> m_GuideSpecRadianceHitDist; // RGB=specular, A=hit distance (RGBA32F)
        std::shared_ptr<Walnut::Image> m_GuideEmission;            // RGB=direct emission (RGBA32F)
        std::shared_ptr<Walnut::Image> m_GuideDepth;               // NDC depth (RGBA32F)

        // FSR upscaler
        std::unique_ptr<FSRUpscaler> m_FSRUpscaler;
        std::shared_ptr<Walnut::Image> m_FinalImageUpscaled;
        uint32_t m_RenderWidth{ 0 };
        uint32_t m_RenderHeight{ 0 };
        uint32_t m_DisplayWidth{ 0 };
        uint32_t m_DisplayHeight{ 0 };
        bool     m_FSREnabled{ false };
        uint32_t m_FSRQuality{ 1 };
        float    m_FSRSharpness{ 0.0f };

        // Frame counter for temporal accumulation (resets on scene change)
        uint32_t m_FrameIndex{ 0 };
        // Global tick counter for PRNG seeds (never resets)
        uint32_t m_GlobalTick{ 0 };

        // Dirty tracking for incremental GPU updates
        std::vector<uint32_t> m_DirtyMeshIndices;   // Mesh indices that changed
        std::vector<uint32_t> m_DirtyLightIndices;  // Light indices that changed

        // Acceleration structure
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
        uint32_t m_LastMaxRecursionDepth{ UINT32_MAX }; // UINT32_MAX forces initial creation

        // Cached scene metrics — only recomputed when m_SceneValid is false (scene invalidated)
        size_t   m_CachedTotalMeshCount{ 0 };
        size_t   m_CachedTotalVertices{ 0 };
        size_t   m_CachedTotalIndices{ 0 };
        bool m_SceneValid{ false };
        bool m_FirstFrame{ true };
        bool m_AccumFirstFrame{ true };
        bool m_GuidesFirstFrame{ true };
        glm::mat4 m_LastCameraView{ glm::mat4(0.0f) }; // for temporal accumulation reset on camera move
        glm::mat4 m_LastCameraProjection{ glm::mat4(0.0f) };
        glm::vec2 m_LastCameraJitter{ 0.0f, 0.0f };
        glm::vec2 m_PrevCameraJitter{ 0.0f, 0.0f };
        float m_ElapsedTime{ 0.0f };
        RenderPassStats m_LastPassStats{};

        std::vector<GPUVertex> m_PreviousFrameVertices;
        std::vector<AABBTransform> m_PreviousFrameAABBTransforms;

        // NRD integration wrapper (currently scaffold mode until SDK/backend is wired).
        NRDDenoiser m_NRDDenoiser;

        // Texture cache
        std::unordered_map<std::string, std::shared_ptr<Walnut::Image>> m_TextureCache;
    };
}  // namespace Vlkrt
