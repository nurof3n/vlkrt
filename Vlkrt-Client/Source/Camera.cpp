#include "Camera.h"

#include "Walnut/Input/Input.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace Walnut;

namespace Vlkrt
{
    Camera::Camera(float verticalFOV, float nearClip, float farClip)
        : m_VerticalFOV(verticalFOV), m_NearClip(nearClip), m_FarClip(farClip)
    {
        m_ForwardDirection = glm::vec3(0, 0, -1);
        m_Position         = glm::vec3(0, 2, 10);
    }

    auto Camera::OnUpdate(float ts) -> bool
    {
        glm::vec2 mousePos  = Input::GetMousePosition();
        glm::vec2 delta     = (mousePos - m_LastMousePosition) * 0.002f;
        m_LastMousePosition = mousePos;

        // Movement is only allowed when the right mouse button is held
        if (!Input::IsMouseButtonDown(MouseButton::Right)) {
            Input::SetCursorMode(CursorMode::Normal);
            return false;
        }

        Input::SetCursorMode(CursorMode::Locked);

        bool moved = false;

        constexpr glm::vec3 upDirection(0.0f, 1.0f, 0.0f);
        glm::vec3 rightDirection = glm::cross(m_ForwardDirection, upDirection);

        // Movement
        if (Input::IsKeyDown(KeyCode::W)) {
            m_Position += m_ForwardDirection * m_MovementSpeed * ts;
            moved = true;
        }
        else if (Input::IsKeyDown(KeyCode::S)) {
            m_Position -= m_ForwardDirection * m_MovementSpeed * ts;
            moved = true;
        }
        if (Input::IsKeyDown(KeyCode::A)) {
            m_Position -= rightDirection * m_MovementSpeed * ts;
            moved = true;
        }
        else if (Input::IsKeyDown(KeyCode::D)) {
            m_Position += rightDirection * m_MovementSpeed * ts;
            moved = true;
        }
        if (Input::IsKeyDown(KeyCode::Q)) {
            m_Position -= upDirection * m_MovementSpeed * ts;
            moved = true;
        }
        else if (Input::IsKeyDown(KeyCode::E)) {
            m_Position += upDirection * m_MovementSpeed * ts;
            moved = true;
        }

        // Rotation
        if (delta.x != 0.0f || delta.y != 0.0f) {
            float pitchDelta = delta.y * m_RotationSpeed;
            float yawDelta   = delta.x * m_RotationSpeed;

            glm::quat q = glm::normalize(glm::cross(glm::angleAxis(-pitchDelta, rightDirection),
                    glm::angleAxis(-yawDelta, glm::vec3(0.f, 1.0f, 0.0f))));

            m_ForwardDirection = glm::rotate(q, m_ForwardDirection);
            moved              = true;
        }

        if (moved) { RecalculateView(); }

        return moved;
    }

    void Camera::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) [[unlikely]]
            return;

        if (width == m_ViewportWidth && height == m_ViewportHeight) [[unlikely]]
            return;

        m_ViewportWidth  = width;
        m_ViewportHeight = height;

        RecalculateProjection();
    }

    void Camera::RecalculateProjection()
    {
        m_Projection = glm::perspectiveFov(
                glm::radians(m_VerticalFOV), (float) m_ViewportWidth, (float) m_ViewportHeight, m_NearClip, m_FarClip);
        m_InverseProjection = glm::inverse(m_Projection);
    }

    void Camera::RecalculateView()
    {
        m_View        = glm::lookAt(m_Position, m_Position + m_ForwardDirection, glm::vec3(0, 1, 0));
        m_InverseView = glm::inverse(m_View);
    }
}  // namespace Vlkrt
