#pragma once

#include "Walnut/Layer.h"
#include "Walnut/Networking/Client.h"

#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"

#include <mutex>
#include <glm/glm.hpp>

namespace Vlkrt
{
    class ClientLayer : public Walnut::Layer
    {
    public:
        // Struct to hold player data received from the server
        struct PlayerData
        {
            glm::vec2 Position;
            glm::vec2 Velocity;
        };

    public:
        ClientLayer();

        void OnAttach() override;
        void OnDetach() override;

        void OnUpdate(float ts) override;
        void OnRender() override;
        void OnUIRender() override;

    private:
        void OnDataReceived(const Walnut::Buffer& buffer);
        void UpdateScene();

    private:
        // Client player data
        const float m_Speed{ 100.0f };
        glm::vec2   m_PlayerPosition{ 50.0f, 50.0f };
        glm::vec2   m_PlayerVelocity{};

        // Server player data
        std::mutex                     m_PlayerDataMutex;
        std::map<uint32_t, PlayerData> m_PlayerData;

        // Networking
        std::string    m_ServerAddress;
        Walnut::Client m_Client;
        uint32_t       m_PlayerID{};

        // Rendering
        Renderer m_Renderer;
        Camera   m_Camera;
        Scene    m_Scene;
        uint32_t m_ViewportWidth{};
        uint32_t m_ViewportHeight{};
        float    m_LastRenderTime{};

        // Scene change tracking
        glm::vec2 m_LastPlayerPosition{};
        size_t    m_LastPlayerCount    = 0;
        bool      m_NetworkDataChanged = false;
    };
}  // namespace Vlkrt