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
    // -------------------------------------------------------------------------
    // Enumerations
    // -------------------------------------------------------------------------

    enum class LightType : uint32_t
    {
        Square      = 0,
        Directional = 1,
    };

    enum class AnalyticPrimitiveType : uint32_t
    {
        AABB   = 0,
        Sphere = 1,
    };

    enum class SDFPrimitiveType : uint32_t
    {
        IntersectedRoundCube = 0,
        SquareTorus          = 1,
        Cog                  = 2,
        Cylinder             = 3,
        SolidAngle           = 4,
    };

    enum class RaytracingMode : uint32_t
    {
        PathTracing         = 1,
        PathTracingTemporal = 2,
    };

    enum class ImportanceSamplingMode : uint32_t
    {
        Uniform = 0,
        Cosine  = 1,
        BSDF    = 2,
    };

    enum class NRDGuideDebugViewMode : uint32_t
    {
        FinalImage            = 0,
        NormalRoughness       = 1,
        ViewZ                 = 2,
        MotionVectors         = 3,
        DiffRadianceHitDist   = 4,
        SpecRadianceHitDist   = 5,
    };

    // -------------------------------------------------------------------------
    // CPU material — Disney BSDF parameters.
    // Old YAML files that supply only albedo/shininess/specular are auto-converted
    // on load: Phong shininess drives roughness, specular drives metallic.
    // -------------------------------------------------------------------------
    struct Material
    {
        std::string Name;

        // Base colour
        glm::vec3 Albedo{ 1.0f };

        // Emission (for emissive / light-mesh materials)
        glm::vec3 Emission{ 0.0f };

        // Volume
        glm::vec3 Extinction{ 1.0f };
        float     AtDistance{ 1.0f };

        // Disney BSDF lobes
        float Roughness            { 0.5f };
        float Metallic             { 0.0f };
        float Subsurface           { 0.0f };
        float Anisotropic          { 0.0f };
        float Sheen                { 0.0f };
        float SheenTint            { 0.5f };
        float Clearcoat            { 0.0f };
        float ClearcoatGloss       { 1.0f };
        float SpecularTint         { 0.0f };
        float SpecularTransmission { 0.0f };
        float Eta                  { 1.5f };

        // Procedural-primitive step scale (SDF ray march)
        float StepScale{ 1.0f };

        // Internal classification (0 = regular, matches GLSL materialIndex)
        uint32_t MaterialIndex{ 0 };

        // >=0 when this material belongs to a light mesh; index into g_lights
        int32_t LightIndex{ -1 };

        // Texture
        std::string TextureFilename;  // empty = use Albedo
        float       Tiling{ 1.0f };
    };

    /**
     * @brief CPU vertex definition.
     */
    struct Vertex
    {
        glm::vec3 Position{};
        glm::vec3 Normal{};
        glm::vec2 TexCoord{};
    };

    /**
     * @brief Mesh definition containing CPU vertex/index data and material reference.
     */
    struct Mesh
    {
        std::string Filename;  // Source filename (if empty, mesh was created procedurally)
        std::string Name;
        std::vector<Vertex> Vertices;
        std::vector<uint32_t> Indices;

        int MaterialIndex{ 0 };

        glm::mat4 Transform = glm::mat4(1.0f);
    };

    /**
     * @brief CPU light definition matching the GLSL GPULight layout.
     *  Type 0 = Square area light (has Position + Size)
     *  Type 1 = Directional light (has Direction + Intensity)
     */
    struct Light
    {
        glm::vec3 Position{};        // World position
        float     Intensity{ 1.0f }; // Brightness
        glm::vec3 Emission{ 1.0f };  // Colour/radiance (was Color)
        float     Size{ 1.825f };    // Area light half-size (was Radius)
        glm::vec3 Direction{ 0.0f }; // For directional lights (normalised)
        LightType Type{ LightType::Square };
    };

    /**
     * @brief Decomposed, hierarchical transform with position, rotation (quaternion), and scale.
     */
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

    // -------------------------------------------------------------------------
    // Procedural geometry entity (AABB-based, analytic or SDF)
    // -------------------------------------------------------------------------
    struct ProceduralEntity
    {
        std::string Name;
        glm::mat4   Transform{ 1.0f };  // Local-to-world
        bool        IsAnalytic{ true }; // true=analytic BLAS, false=SDF BLAS
        uint32_t    PrimitiveType{ 0 }; // AnalyticPrimitiveType or SDFPrimitiveType
        int         MaterialIndex{ 0 }; // Index into Scene::Materials
    };

    /**
     * @brief Type of scene entity, used for determining how to render and update each node in the hierarchy.
     */
    enum class EntityType
    {
        Empty,      // Transform-only, used for grouping
        Mesh,       // Mesh with material
        Light,      // Light source
        Camera,     // Camera
        Procedural  // Analytic/SDF procedural shape
    };

    /**
     * @brief Scene entity that supports hierarchical transforms, type-specific data, and scripting.
     */
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
            float     Intensity{ 1.0f };
            LightType Type{ LightType::Square };
            float     Size{ 1.825f };
        } LightData;

        struct CameraData
        {
            float FOV{ 45.0f };
            float Near{ 0.1f };
            float Far{ 100.0f };
        } CameraData;

        struct ProceduralData
        {
            bool     IsAnalytic{ true };   // true=analytic BLAS, false=SDF BLAS
            uint32_t PrimitiveType{ 0 };   // AnalyticPrimitiveType or SDFPrimitiveType
            int      MaterialIndex{ 0 };   // Index into Scene::Materials
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

    /**
     * @brief Flat scene definition.
     */
    struct Scene
    {
        std::vector<Mesh>              StaticMeshes;
        std::vector<Mesh>              DynamicMeshes;
        std::vector<Material>          Materials;
        std::vector<Light>             Lights;
        std::vector<ProceduralEntity>  ProceduralEntities;

        // ---- Rendering parameters (written into SceneUBO each frame) ----
        RaytracingMode        RaytracingType             { RaytracingMode::PathTracing };
        ImportanceSamplingMode ImportanceSampling        { ImportanceSamplingMode::BSDF };
        uint32_t              MaxRecursionDepth          { 8 };   // hard cap enforced by UI: 12
        uint32_t              MaxShadowRecursionDepth    { 8 };   // default = MaxRecursionDepth
        uint32_t              PathSqrtSamplesPerPixel    { 1 };
        bool                  ApplyJitter                { true };
        bool                  OnlyOneLightSample         { false };
        uint32_t              RussianRouletteDepth       { 3 };
        bool                  AnisotropicBSDF            { true };
        bool                  EnableNRDDenoiser          { false };
        NRDGuideDebugViewMode NRDGuideDebugView          { NRDGuideDebugViewMode::FinalImage };
        uint32_t              SceneIndex                 { 0 };  // 0=Custom,1=Demo,2=Cornell,3=PbrShowcase
        glm::vec3             BackgroundColor            { 0.0f };

        // Optional camera hint loaded from YAML scene_settings
        bool      HasCameraHint   { false };
        glm::vec3 CameraPosition  { 0.0f, 3.0f, 10.0f };
        glm::vec3 CameraTarget    { 0.0f, 0.0f, 0.0f };
    };

    /**
     * @brief Manages the scene hierarchy for efficient transform updates.
     */
    class SceneHierarchy
    {
    public:
        void SetLocalTransform(SceneEntity& entity, const Transform& newTransform)
        {
            entity.SetLocalTransform(newTransform);
        }

        void UpdateDirtyTransforms(SceneEntity& root, const glm::mat4& parentWorld = glm::mat4(1.0f))
        {
            UpdateDirtyTransformsRecursive(root, parentWorld);
        }

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
