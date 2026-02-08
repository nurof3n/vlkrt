#pragma once

#include "Walnut/Layer.h"
#include "Walnut/Networking/Client.h"

#include <mutex>
#include <glm/glm.hpp>

namespace Vlkrt
{
    class ClientLayer : public Walnut::Layer
    {
    public:
        struct PlayerData
        {
            glm::vec2 Position;
            glm::vec2 Velocity;
        };

    public:
        void OnAttach() override;
        void OnDetach() override;

        void OnUpdate(float ts) override;
        void OnRender() override;
        void OnUIRender() override;

    private:
        void OnDataReceived(const Walnut::Buffer& buffer);

    private:
        const float m_Speed{ 100.0f };

        glm::vec2 m_PlayerPosition{ 50.0f, 50.0f };
        glm::vec2 m_PlayerVelocity{};

        std::string m_ServerAddress;

        Walnut::Client m_Client;
        uint32_t       m_PlayerID{};

        std::mutex                     m_PlayerDataMutex;
        std::map<uint32_t, PlayerData> m_PlayerData;
    };
}  // namespace Vlkrt