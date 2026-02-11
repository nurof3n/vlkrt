#pragma once

#include "Walnut/Layer.h"
#include "Walnut/Networking/Client.h"

#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "SceneLoader.h"
#include "MeshLoader.h"

#include <mutex>
#include <glm/glm.hpp>
#include <filesystem>
#include <vector>
#include <string>

namespace Vlkrt
{
    /**
     * @brief ClientLayer is the main application layer for the Vlkrt client. It manages the connection to the server,
     * handles player input, receives updates from the server, and renders the scene. It also includes a hierarchical
     * ImGui scene editor.
     */
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
        ClientLayer() = default;

        void OnAttach() override;
        void OnDetach() override;

        void OnUpdate(float ts) override;
        void OnRender() override;
        void OnUIRender() override;

    private:
        void OnDataReceived(const Walnut::Buffer& buffer);
        void UpdateScene();
        void RunScripts(SceneEntity& entity, float ts);
        void LoadScene(const std::string& scenePath);
        void SyncSceneToHierarchy();
        void SaveScene();

        // Resource discovery
        void RefreshResources();

        // Hierarchical ImGui scene editor functions
        void ImGuiRenderSceneHierarchy();
        void ImGuiRenderEntity(SceneEntity& entity, const glm::mat4& parentWorldTransform);
        void ImGuiRenderTransformControls(Transform& localTransform, const std::string& id);
        void ImGuiRenderEntityProperties(SceneEntity& entity);
        void FlattenHierarchyToScene(const SceneEntity& entity, const glm::mat4& parentWorld);

    private:
        bool m_TexturesLoaded{ false };

        // Client player data
        const float m_Speed{ 100.0f };
        glm::vec2 m_PlayerPosition{ 50.0f, 50.0f };
        glm::vec2 m_PlayerVelocity{};

        // Server player data
        std::mutex m_PlayerDataMutex;
        std::map<uint32_t, PlayerData> m_PlayerData;

        // Networking
        std::string m_ServerAddress;
        Walnut::Client m_Client;
        uint32_t m_PlayerID{};
        bool m_NetworkDataChanged = false;

        // Rendering
        Camera m_Camera{ 45.0f, 0.1f, 100.0f };
        Renderer m_Renderer;
        Scene m_Scene;
        uint32_t m_ViewportWidth{};
        uint32_t m_ViewportHeight{};
        float m_LastRenderTime{};

        // Scene management
        std::string m_CurrentScene{ "default" };
        std::string m_SelectedScene{ "default" };

        // Hierarchical scene data
        SceneEntity m_SceneRoot;
        SceneHierarchy m_SceneHierarchy;
        HierarchyMapping m_HierarchyMapping;

        // Scene change tracking
        glm::vec2 m_LastPlayerPosition{};
        size_t m_LastPlayerCount{ 0 };

        // Resource cache
        std::vector<std::string> m_AvailableTextures;
        std::vector<std::string> m_AvailableModels;
        std::vector<std::string> m_AvailableScenes;
        std::vector<std::string> m_AvailableScripts;
    };
}  // namespace Vlkrt