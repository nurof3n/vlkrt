#pragma once

#include "Walnut/Image.h"

#include <memory>
#include <vector>
#include <glm/glm.hpp>


namespace Vlkrt
{
    class Camera;
    class Scene;

    class Renderer
    {
    public:
        Renderer() = default;

        void OnResize(uint32_t width, uint32_t height);
        void Render(const Scene& scene, const Camera& camera);

        std::shared_ptr<Walnut::Image> GetFinalImage() const
        {
            return m_FinalImage;
        }

    private:
        struct HitPayload
        {
            float     HitDistance;
            glm::vec3 WorldPosition;
            glm::vec3 WorldNormal;

            int ObjectIndex;
        };

        glm::vec4 PerPixel(uint32_t x, uint32_t y);  // RayGen

        HitPayload TraceRay(const glm::vec3& origin, const glm::vec3& direction);
        HitPayload ClosestHit(const glm::vec3& origin, const glm::vec3& direction, float hitDistance, int objectIndex);
        HitPayload Miss(const glm::vec3& origin, const glm::vec3& direction);

    private:
        std::shared_ptr<Walnut::Image> m_FinalImage;

        std::vector<uint32_t> m_ImageHorizontalIter, m_ImageVerticalIter;

        const Scene*  m_ActiveScene  = nullptr;
        const Camera* m_ActiveCamera = nullptr;

        uint32_t* m_ImageData = nullptr;
    };
}  // namespace Vlkrt
