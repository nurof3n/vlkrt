#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Vlkrt
{
    /**
     * @brief A simple camera class that handles projection and view matrices, as well as ray direction calculations for
     * screen-space effects.
     */
    class Camera
    {
    public:
        Camera(float verticalFOV, float nearClip, float farClip);

        // Returns true if the camera was moved or rotated, false otherwise
        auto OnUpdate(float ts) -> bool;
        void OnResize(uint32_t width, uint32_t height);

        const auto& GetProjection() const { return m_Projection; }
        const auto& GetInverseProjection() const { return m_InverseProjection; }
        const auto& GetView() const { return m_View; }
        const auto& GetInverseView() const { return m_InverseView; }
        const auto& GetPosition() const { return m_Position; }
        const auto& GetDirection() const { return m_ForwardDirection; }

    private:
        void RecalculateProjection();
        void RecalculateView();

    private:
        glm::mat4 m_Projection{ 1.0f };
        glm::mat4 m_View{ 1.0f };
        glm::mat4 m_InverseProjection{ 1.0f };
        glm::mat4 m_InverseView{ 1.0f };

        float m_VerticalFOV{ 45.0f };
        float m_NearClip{ 0.1f };
        float m_FarClip{ 100.0f };

        float m_MovementSpeed{ 5.0f };
        float m_RotationSpeed{ 0.3f };

        glm::vec3 m_Position{};
        glm::vec3 m_ForwardDirection{};

        glm::vec2 m_LastMousePosition{};

        uint32_t m_ViewportWidth{};
        uint32_t m_ViewportHeight{};
    };
}  // namespace Vlkrt
