#include "Renderer.h"

#include "Walnut/Random.h"

#include "Camera.h"
#include "Scene.h"

#include <execution>

namespace Utils
{

    static uint32_t ConvertToRGBA(const glm::vec4& color)
    {
        uint8_t r = (uint8_t) (color.r * 255.0f);
        uint8_t g = (uint8_t) (color.g * 255.0f);
        uint8_t b = (uint8_t) (color.b * 255.0f);
        uint8_t a = (uint8_t) (color.a * 255.0f);

        uint32_t result = (a << 24) | (b << 16) | (g << 8) | r;
        return result;
    }

}  // namespace Utils

namespace Vlkrt
{

    void Renderer::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return;

        if (m_FinalImage) {
            // No resize necessary
            if (m_FinalImage->GetWidth() == width && m_FinalImage->GetHeight() == height)
                return;

            m_FinalImage->Resize(width, height);
        }
        else {
            m_FinalImage = std::make_shared<Walnut::Image>(width, height, Walnut::ImageFormat::RGBA);
        }

        delete[] m_ImageData;
        m_ImageData = new uint32_t[width * height];

        m_ImageHorizontalIter.resize(width);
        m_ImageVerticalIter.resize(height);
        for (uint32_t i = 0; i < width; i++)
            m_ImageHorizontalIter[i] = i;
        for (uint32_t i = 0; i < height; i++)
            m_ImageVerticalIter[i] = i;
    }

    void Renderer::Render(const Scene& scene, const Camera& camera)
    {
        m_ActiveScene  = &scene;
        m_ActiveCamera = &camera;

        if (!m_FinalImage)
            return;

#define MT 1
#if MT
        std::for_each(std::execution::par, m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(), [this](uint32_t y) {
            std::for_each(std::execution::par, m_ImageHorizontalIter.begin(), m_ImageHorizontalIter.end(),
                    [this, y](uint32_t x) {
                        glm::vec4 color = PerPixel(x, y);
                        color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
                        m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertToRGBA(color);
                    });
        });
#else
        for (uint32_t y = 0; y < m_FinalImage->GetHeight(); y++) {
            for (uint32_t x = 0; x < m_FinalImage->GetWidth(); x++) {
                glm::vec4 color = PerPixel(x, y);
                color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
                m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertToRGBA(color);
            }
        }
#endif

        m_FinalImage->SetData(m_ImageData);
    }

    glm::vec4 Renderer::PerPixel(uint32_t x, uint32_t y)
    {
        glm::vec3 rayOrigin    = m_ActiveCamera->GetPosition();
        glm::vec3 rayDirection = m_ActiveCamera->GetRayDirections()[x + y * m_FinalImage->GetWidth()];

        Renderer::HitPayload payload = TraceRay(rayOrigin, rayDirection);

        if (payload.HitDistance < 0.0f) {
            // Sky color
            return glm::vec4(0.6f, 0.7f, 0.9f, 1.0f);
        }

        // Simple lighting
        const Sphere&   sphere   = m_ActiveScene->Spheres[payload.ObjectIndex];
        const Material& material = m_ActiveScene->Materials[sphere.MaterialIndex];

        glm::vec3 lightDir = glm::normalize(glm::vec3(-1, -1, -1));
        float light = glm::max(glm::dot(payload.WorldNormal, -lightDir), 0.0f) * 0.8f + 0.2f;

        glm::vec3 color = material.Albedo * light;
        return glm::vec4(color, 1.0f);
    }

    Renderer::HitPayload Renderer::TraceRay(const glm::vec3& origin, const glm::vec3& direction)
    {
        int   closestSphere = -1;
        float hitDistance   = std::numeric_limits<float>::max();
        for (size_t i = 0; i < m_ActiveScene->Spheres.size(); i++) {
            const Sphere& sphere = m_ActiveScene->Spheres[i];
            glm::vec3     oc     = origin - sphere.Position;

            float a = glm::dot(direction, direction);
            float b = 2.0f * glm::dot(oc, direction);
            float c = glm::dot(oc, oc) - sphere.Radius * sphere.Radius;

            float discriminant = b * b - 4.0f * a * c;
            if (discriminant < 0.0f)
                continue;

            float closestT = (-b - glm::sqrt(discriminant)) / (2.0f * a);
            if (closestT > 0.0f && closestT < hitDistance) {
                hitDistance   = closestT;
                closestSphere = (int) i;
            }
        }

        if (closestSphere < 0)
            return Miss(origin, direction);

        return ClosestHit(origin, direction, hitDistance, closestSphere);
    }

    Renderer::HitPayload Renderer::ClosestHit(
            const glm::vec3& origin, const glm::vec3& direction, float hitDistance, int objectIndex)
    {
        Renderer::HitPayload payload;
        payload.HitDistance = hitDistance;
        payload.ObjectIndex = objectIndex;

        const Sphere& closestSphere = m_ActiveScene->Spheres[objectIndex];

        glm::vec3 origin0     = origin - closestSphere.Position;
        payload.WorldPosition = origin + direction * hitDistance;
        payload.WorldNormal   = glm::normalize(payload.WorldPosition - closestSphere.Position);

        payload.WorldPosition += payload.WorldNormal * 0.0001f;

        return payload;
    }

    Renderer::HitPayload Renderer::Miss(const glm::vec3& origin, const glm::vec3& direction)
    {
        Renderer::HitPayload payload;
        payload.HitDistance = -1.0f;
        return payload;
    }
}  // namespace Vlkrt