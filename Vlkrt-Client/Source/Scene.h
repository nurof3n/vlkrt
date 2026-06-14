#pragma once

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

namespace Vlkrt
{
    enum class LightType : uint32_t
    {
        Square,
        Directional,
    };

    enum class AnalyticPrimitiveType : uint32_t
    {
        AABB,
        Sphere,
    };

    enum class SDFPrimitiveType : uint32_t
    {
        IntersectedRoundCube,
        SquareTorus,
        Cog,
        Cylinder,
        SolidAngle,
    };

    enum class RaytracingMode : uint32_t
    {
        PathTracing         = 1,
        PathTracingTemporal = 2,
    };

    enum class ImportanceSamplingMode : uint32_t
    {
        Uniform,
        Cosine,
        BSDF,
    };

    enum class NRDGuideDebugViewMode : uint32_t
    {
        FinalImage,
        NormalRoughness,
        ViewZ,
        MotionVectors,
        DiffRadianceHitDist,
        SpecRadianceHitDist,
    };

    /// <summary>
    /// CPU material definition matching the GLSL GPUMaterial layout.
    /// </summary>
    struct Material
    {
        std::string Name;

        glm::vec3 Albedo{ 1.0f };
        glm::vec3 Emission{ 0.0f };
        glm::vec3 Extinction{ 1.0f };
        float AtDistance{ 1.0f };

        // Disney BSDF lobes
        float Roughness{ 0.5f };
        float Metallic{ 0.0f };
        float Subsurface{ 0.0f };
        float Anisotropic{ 0.0f };
        float Sheen{ 0.0f };
        float SheenTint{ 0.5f };
        float Clearcoat{ 0.0f };
        float ClearcoatGloss{ 1.0f };
        float SpecularTint{ 0.0f };
        float SpecularTransmission{ 0.0f };
        float Eta{ 1.5f };

        // Procedural-primitive step scale (SDF ray march)
        float StepScale{ 1.0f };

        uint32_t MaterialIndex{ 0 };
        int32_t LightIndex{ -1 };

        std::string TextureFilename;
        float Tiling{ 1.0f };
    };

    /// <summary>
    /// CPU vertex definition matching the GLSL GPUVertex layout.
    /// </summary>
    struct Vertex
    {
        glm::vec3 Position{};
        glm::vec3 Normal{};
        glm::vec2 TexCoord{};
    };

    /// <summary>
    /// Mesh definition containing CPU vertex/index data and material reference.
    /// </summary>
    struct Mesh
    {
        std::string Filename;
        std::string Name;
        std::vector<Vertex> Vertices;
        std::vector<uint32_t> Indices;
        glm::mat4 Transform = glm::mat4(1.0f);
        uint32_t MaterialIndex{ 0 };
    };

    /// <summary>
    /// CPU light definition matching the GLSL GPULight layout.
    /// </summary>
    struct Light
    {
        glm::vec3 Position{};
        float Intensity{ 1.0f };
        glm::vec3 Emission{ 1.0f };
        float Size{ 1.825f };  // Area light half-size
        glm::vec3 Direction{ 0.0f };
        LightType Type{ LightType::Square };
    };

    /// <summary>
    /// Decomposed, hierarchical transform with position, rotation (quaternion), and scale.
    /// </summary>
    struct Transform
    {
        glm::vec3 Position{};
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };

        auto GetLocalMatrix() const -> glm::mat4
        {
            auto translation = glm::translate(glm::mat4(1.0f), Position);
            auto rotation    = glm::mat4_cast(Rotation);
            auto scale       = glm::scale(glm::mat4(1.0f), Scale);
            return translation * rotation * scale;
        }

        auto GetWorldMatrix(const glm::mat4& parentWorld) const -> glm::mat4 { return parentWorld * GetLocalMatrix(); }
    };

    /// <summary>
    /// Procedural geometry entity.
    /// </summary>
    struct ProceduralEntity
    {
        std::string Name;
        glm::mat4 Transform{ 1.0f };  // Local-to-world
        bool IsAnalytic{ true };      // true=analytic, false=SDF
        uint32_t PrimitiveType{ 0 };
        int MaterialIndex{ 0 };
    };

    /// <summary>
    /// Type of scene entity, used for determining how to render and update each node in the hierarchy.
    /// </summary>
    enum class EntityType
    {
        Empty,  // Transform-only, used for grouping
        Mesh,
        Light,
        Camera,
        Procedural,
    };

    /// <summary>
    /// Scene entity that supports hierarchical transforms, type-specific data, and scripting.
    /// </summary>
    struct SceneEntity
    {
        std::string Name;
        EntityType Type{ EntityType::Empty };
        Transform LocalTransform;
        glm::mat4 WorldTransform{ 1.0f };

        std::string ScriptPath;
        bool ScriptInitialized{ false };

        bool IsDirty{ true };
        SceneEntity* Parent{ nullptr };

        std::vector<SceneEntity> Children;

        struct MeshData
        {
            std::string Filename;
            int MaterialIndex{};
        } MeshData;

        struct LightData
        {
            glm::vec3 Emission{ 1.0f };
            float Intensity{ 1.0f };
            LightType Type{ LightType::Square };
            float Size{ 1.825f };
        } LightData;

        struct CameraData
        {
            float FOV{ 45.0f };
            float Near{ 0.1f };
            float Far{ 100.0f };
        } CameraData;

        struct ProceduralData
        {
            bool IsAnalytic{ true };      // true=analytic BLAS, false=SDF BLAS
            uint32_t PrimitiveType{ 0 };  // AnalyticPrimitiveType or SDFPrimitiveType
            int MaterialIndex{ 0 };       // Index into Scene::Materials
        } ProceduralData;

        void MarkDirtyRecursive()
        {
            IsDirty = true;
            for (auto& child : Children) child.MarkDirtyRecursive();
        }

        void SetLocalTransform(const Transform& newTransform)
        {
            LocalTransform = newTransform;
            IsDirty        = true;
        }
    };

    /// <summary>
    /// Flat scene definition.
    /// </summary>
    struct Scene
    {
        std::vector<Mesh> StaticMeshes;
        std::vector<Mesh> DynamicMeshes;
        std::vector<Material> Materials;
        std::vector<Light> Lights;
        std::vector<ProceduralEntity> ProceduralEntities;

        // Rendering parameters
        RaytracingMode RaytracingType{ RaytracingMode::PathTracing };
        ImportanceSamplingMode ImportanceSampling{ ImportanceSamplingMode::BSDF };
        uint32_t MaxRecursionDepth{ 8 };
        uint32_t MaxShadowRecursionDepth{ 8 };
        uint32_t PathSqrtSamplesPerPixel{ 1 };
        bool ApplyJitter{ true };
        bool OnlyOneLightSample{ false };
        uint32_t RussianRouletteDepth{ 3 };
        bool AnisotropicBSDF{ true };
        bool EnableNRDDenoiser{ false };
        NRDGuideDebugViewMode NRDGuideDebugView{ NRDGuideDebugViewMode::FinalImage };
        // Live RELAX tuning (applied at runtime without restart)
        float NRDMinMaterialForDiffuse{ 0.0f };
        float NRDMinMaterialForSpecular{ 0.0f };
        float NRDDiffusePrepassBlurRadius{ 6.0f };
        float NRDSpecularPrepassBlurRadius{ 10.0f };
        uint32_t NRDDiffuseMaxAccumulatedFrameNum{ 10 };
        uint32_t NRDSpecularMaxAccumulatedFrameNum{ 12 };
        uint32_t NRDDiffuseMaxFastAccumulatedFrameNum{ 2 };
        uint32_t NRDSpecularMaxFastAccumulatedFrameNum{ 3 };
        float NRDAntilagAccelerationAmount{ 0.9f };
        float NRDAntilagSpatialSigmaScale{ 2.5f };
        float NRDAntilagTemporalSigmaScale{ 0.22f };
        float NRDAntilagResetAmount{ 1.0f };
        float NRDDisocclusionThreshold{ 0.010f };
        float NRDDisocclusionThresholdAlternate{ 0.035f };
        bool EnableFSR{ false };
        uint32_t FSRQualityMode{ 1 };  // 1 = Quality
        float FSRSharpness{ 0.0f };
        uint32_t SceneIndex{ 0 };
        glm::vec3 BackgroundColor{ 0.0f };

        // Optional camera hint loaded from YAML scene_settings
        bool HasCameraHint{ false };
        glm::vec3 CameraPosition{ 0.0f, 3.0f, 10.0f };
        glm::vec3 CameraTarget{ 0.0f, 0.0f, 0.0f };
    };

    /// <summary>
    /// Manages the scene hierarchy for efficient transform updates.
    /// </summary>
    class SceneHierarchy
    {
    public:
        void SetLocalTransform(SceneEntity& entity, const Transform& newTransform)
        { entity.SetLocalTransform(newTransform); }

        void UpdateDirtyTransforms(SceneEntity& root, const glm::mat4& parentWorld = glm::mat4(1.0f))
        { UpdateDirtyTransformsRecursive(root, parentWorld); }

    private:
        void UpdateDirtyTransformsRecursive(SceneEntity& entity, const glm::mat4& parentWorld)
        {
            // Recompute world transform if this node or its parent is dirty
            if (entity.IsDirty) {
                entity.WorldTransform = entity.LocalTransform.GetWorldMatrix(parentWorld);
                entity.IsDirty        = false;
            }

            // Recursively update children with this node's world transform
            for (auto& child : entity.Children) { UpdateDirtyTransformsRecursive(child, entity.WorldTransform); }
        }
    };
}  // namespace Vlkrt
