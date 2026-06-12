#pragma once

#include "Scene.h"
#include <functional>

namespace Vlkrt
{
    /**
     * @brief Static factory for the three DieXaR reference scenes.
     *
     * Each method returns a fully initialised Scene (meshes, materials, lights,
     * procedural entities and rendering parameters).  The Camera position is
     * returned through the optional out-param so the caller can move the
     * camera to the canonical view used in DieXaR.
     */
    class SceneFactory
    {
    public:
        // Scene index constants (match DieXaR SceneIndex / Scene::SceneIndex)
        static constexpr uint32_t SCENE_CORNELL_BOX  = 1;
        static constexpr uint32_t SCENE_DEMO         = 2;
        static constexpr uint32_t SCENE_PBR_SHOWCASE = 3;

        struct CameraHint
        {
            glm::vec3 Eye{};      // World-space camera position
            glm::vec3 Target{};   // Look-at point
        };

        static auto CreateCornellBox (CameraHint* camHint = nullptr) -> Scene;
        static auto CreateDemo       (CameraHint* camHint = nullptr) -> Scene;
        static auto CreatePbrShowcase(CameraHint* camHint = nullptr) -> Scene;

    private:
        // Helper: build a unit quad (two triangles) with given normal
        static void AddQuad(Mesh& mesh,
                const glm::vec3& p0, const glm::vec3& p1,
                const glm::vec3& p2, const glm::vec3& p3,
                const glm::vec3& normal, int matIdx);

        // Helper: add a sphere from a procedural entity record
        static ProceduralEntity MakeSphere(
                const std::string& name,
                const glm::vec3& center, float radius,
                int materialIndex);

        // Helper: add an AABB analytic entity (unit cube AABB, transformed)
        static ProceduralEntity MakeAABB(
                const std::string& name,
                const glm::mat4& transform,
                int materialIndex);
    };
}  // namespace Vlkrt
