#include "ClientLayer.h"
#include "ServerPacket.h"
#include "SceneLoader.h"
#include "ScriptEngine.h"
#include "Utils.h"

#include "Walnut/Application.h"
#include "Walnut/Input/Input.h"
#include "Walnut/ImGui/ImGuiTheme.h"
#include "Walnut/Timer.h"
#include "Walnut/Serialization/BufferStream.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <algorithm>


namespace Vlkrt
{
    static Walnut::Buffer s_ScratchBuffer{};

    void ClientLayer::OnAttach()
    {
        ScriptEngine::Init();
        RefreshResources();
        s_ScratchBuffer.Allocate(10 * 1024 * 1024);  // 10 MB

        m_Client.SetDataReceivedCallback([this](const Walnut::Buffer& buffer) { OnDataReceived(buffer); });

        LoadScene("default");
    }

    void ClientLayer::RefreshResources()
    {
        m_AvailableTextures.clear();
        m_AvailableModels.clear();
        m_AvailableScenes.clear();
        m_AvailableScripts.clear();

        auto scanDirectory = [](const std::string& path, std::vector<std::string>& list,
                                     const std::vector<std::string>& extensions, bool stripExtension = false) {
            if (!std::filesystem::exists(path)) return;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (extensions.empty()
                            || std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                        if (stripExtension)
                            list.push_back(entry.path().stem().string());
                        else
                            list.push_back(entry.path().filename().string());
                    }
                }
            }
            std::sort(list.begin(), list.end());
        };

        scanDirectory(TEXTURES_DIR, m_AvailableTextures, { ".jpg", ".jpeg" });
        scanDirectory(MODELS_DIR, m_AvailableModels, { ".obj" });
        scanDirectory(SCENES_DIR, m_AvailableScenes, { ".yaml", ".yml" }, true);
        scanDirectory(SCRIPTS_DIR, m_AvailableScripts, { ".lua" });
    }

    void ClientLayer::OnDetach() { ScriptEngine::Shutdown(); }

    void ClientLayer::OnUpdate(float ts)
    {
        if (!m_TexturesLoaded) {
            m_Renderer.PreloadTextures(m_AvailableTextures);
            m_TexturesLoaded = true;
        }

        RunScripts(m_SceneRoot, ts);

        bool cameraControlMode = Walnut::Input::IsMouseButtonDown(Walnut::MouseButton::Right);
        if (cameraControlMode) {
            // When holding right-click: camera moves with WASD, player stays still
            m_PlayerVelocity = {};
            m_Camera.OnUpdate(ts);
        }
        else {
            // Only process WASD input if ImGui doesn't want keyboard focus
            glm::vec3 dir{ 0.0f };
            if (!ImGui::GetIO().WantCaptureKeyboard) {
                if (Walnut::Input::IsKeyDown(Walnut::KeyCode::W))
                    dir.z = -1.0f;
                else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::S))
                    dir.z = 1.0f;

                if (Walnut::Input::IsKeyDown(Walnut::KeyCode::A))
                    dir.x = -1.0f;
                else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::D))
                    dir.x = 1.0f;
            }

            bool playerMoving = (dir.x != 0.0f || dir.z != 0.0f);

            if (playerMoving) {
                dir              = glm::normalize(dir);
                m_PlayerVelocity = dir * m_Speed;
            }
            else
                m_PlayerVelocity = {};

            m_PlayerPosition += m_PlayerVelocity * ts;

            // Camera only moves when player is not moving
            if (!playerMoving) { m_Camera.OnUpdate(ts); }
        }

        // Client update
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) {
            Walnut::BufferStreamWriter stream(s_ScratchBuffer);
            stream.WriteRaw(PacketType::ClientUpdate);
            stream.WriteRaw<glm::vec3>(m_PlayerPosition);
            stream.WriteRaw<glm::vec3>(m_PlayerVelocity);
            m_Client.SendBuffer(stream.GetBuffer());
        }

        // Only update scene if something actually changed
        size_t currentPlayerCount = m_PlayerData.size();
        if (m_PlayerPosition != m_LastPlayerPosition || currentPlayerCount != m_LastPlayerCount
                || m_NetworkDataChanged) {
            UpdateScene();
            m_Renderer.InvalidateScene();
            m_LastPlayerPosition = m_PlayerPosition;
            m_LastPlayerCount    = currentPlayerCount;
            m_NetworkDataChanged = false;
        }

        // Sync hierarchy changes to flat arrays
        FlattenHierarchyToScene(m_SceneRoot, glm::mat4(1.0f));
        m_Renderer.InvalidateScene();
    }

    void ClientLayer::OnRender()
    {
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) [[likely]] {
            Walnut::Timer timer;
            m_Renderer.Render(m_Scene, m_Camera);
            m_LastRenderTime = timer.ElapsedMillis();
        }
    }

    void ClientLayer::OnUIRender()
    {
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) [[likely]] {
            ImGuiViewport* viewport    = ImGui::GetMainViewport();
            uint32_t newViewportWidth  = (uint32_t) viewport->Size.x;
            uint32_t newViewportHeight = (uint32_t) viewport->Size.y;

            // Check if we need to resize
            if (newViewportWidth != m_ViewportWidth || newViewportHeight != m_ViewportHeight) {
                m_ViewportWidth  = newViewportWidth;
                m_ViewportHeight = newViewportHeight;
                m_Renderer.OnResize(m_ViewportWidth, m_ViewportHeight);
                m_Camera.OnResize(m_ViewportWidth, m_ViewportHeight);
            }

            // Render raytraced image as background
            auto image = m_Renderer.GetFinalImage();
            if (image) {
                ImGui::GetBackgroundDrawList()->AddImage(image->GetDescriptorSet(), viewport->Pos,
                        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), ImVec2(0, 1),
                        ImVec2(1, 0));
            }

            // Stats panel overlay
            ImGui::Begin("Stats");
            ImGui::Text("Last render: %.3fms", m_LastRenderTime);
            ImGui::Text("Player ID: %u", m_PlayerID);
            ImGui::Text("Players: %zu", m_PlayerData.size());
            ImGui::End();

            // Chat panel
            ImGuiRenderChatPanel();

            // Scene hierarchy editor panel
            ImGuiRenderSceneHierarchy();
        }
        else {
            auto connectionStatus = m_Client.GetConnectionStatus();
            auto readOnly         = (connectionStatus == Walnut::Client::ConnectionStatus::Connecting);

            ImGui::Begin("Connect to Server");

            ImGui::InputText("Server Address", &m_ServerAddress, readOnly ? ImGuiInputTextFlags_ReadOnly : 0);
            if (connectionStatus == Walnut::Client::ConnectionStatus::Connecting) {
                ImGui::TextColored(ImColor(Walnut::UI::Colors::Theme::textDarker), "Connecting to server...");
            }
            else if (connectionStatus == Walnut::Client::ConnectionStatus::FailedToConnect) {
                ImGui::TextColored(ImColor(Walnut::UI::Colors::Theme::error), "Failed to connect to server.");
            }
            if (ImGui::Button("Connect")) { m_Client.ConnectToServer(m_ServerAddress); }

            ImGui::End();
        }
    }

    void ClientLayer::UpdateScene()
    {
        // Clear only dynamic meshes
        m_Scene.DynamicMeshes.clear();

        // Add current player as cube mesh
        const float cubeSize     = 1.0f;
        Mesh playerMesh          = MeshLoader::GenerateCube(cubeSize);
        playerMesh.MaterialIndex = 0;
        glm::vec3 playerPos      = m_PlayerPosition + glm::vec3(0.0f, cubeSize * 0.5f, 0.0f);
        playerMesh.Transform     = glm::translate(glm::mat4(1.0f), playerPos);
        m_Scene.DynamicMeshes.push_back(playerMesh);

        // Add other players as cube meshes
        m_PlayerDataMutex.lock();
        for (const auto& [playerID, playerData] : m_PlayerData) {
            if (playerID == m_PlayerID) continue;

            Mesh otherPlayerMesh          = MeshLoader::GenerateCube(cubeSize);
            otherPlayerMesh.MaterialIndex = 1;
            glm::vec3 otherPos            = playerData.Position + glm::vec3(0.0f, cubeSize * 0.5f, 0.0f);
            otherPlayerMesh.Transform     = glm::translate(glm::mat4(1.0f), otherPos);
            m_Scene.DynamicMeshes.push_back(otherPlayerMesh);
        }
        m_PlayerDataMutex.unlock();
    }

    void ClientLayer::RunScripts(SceneEntity& entity, float ts)
    {
        if (!entity.ScriptPath.empty()) {
            if (!entity.ScriptInitialized) [[unlikely]]
                ScriptEngine::LoadScript(entity);

            ScriptEngine::CallOnUpdate(entity, ts);
        }

        for (auto& child : entity.Children) RunScripts(child, ts);
    }

    void ClientLayer::LoadScene(const std::string& sceneName)
    {
        auto [scene, root] = SceneLoader::LoadFromYAMLWithHierarchy(sceneName + ".yaml");
        m_Scene            = scene;
        m_SceneRoot        = root;
        m_CurrentScene     = sceneName;
        m_SelectedScene    = sceneName;

        // Create mapping from hierarchy to flat arrays for incremental updates
        m_HierarchyMapping = SceneLoader::CreateMapping(m_SceneRoot, m_Scene);
    }

    void ClientLayer::OnDataReceived(const Walnut::Buffer& data)
    {
        Walnut::BufferStreamReader stream(data);

        PacketType type;
        stream.ReadRaw(type);

        switch (type) {
            case PacketType::ClientConnect:
                stream.ReadRaw(m_PlayerID);
                WL_INFO_TAG("Client", "Connected to server with Player ID: {}", m_PlayerID);
                break;
            case PacketType::Message: {
                ChatMessage msg;
                msg.Deserialize(&stream, msg);
                m_ChatMutex.lock();
                m_ChatHistory.push_back(msg);
                // Keep history limited to 100 messages
                while (m_ChatHistory.size() > 100) { m_ChatHistory.pop_front(); }
                m_ChatMutex.unlock();
                break;
            }
            case PacketType::ClientUpdate:
                m_PlayerDataMutex.lock();
                stream.ReadMap(m_PlayerData);
                m_NetworkDataChanged = true;
                m_PlayerDataMutex.unlock();
                break;
            default: WL_WARN_TAG("Client", "Received unknown packet type: {}", (int) type); break;
        }
    }

    void ClientLayer::SyncSceneToHierarchy()
    {
        // Copy light properties from flat array back to hierarchy
        // NOTE: We only sync properties (color, intensity, radius, direction), NOT position
        // Position is determined by the hierarchy structure and is updated via FlattenEntity
        for (size_t i = 0; i < m_Scene.Lights.size(); ++i) {
            if (i < m_HierarchyMapping.LightIndexToEntity.size()) {
                SceneEntity* lightEntity = m_HierarchyMapping.LightIndexToEntity[i];
                if (lightEntity) {
                    // Sync editable light properties from flat array to hierarchy
                    lightEntity->LightData.Color     = m_Scene.Lights[i].Color;
                    lightEntity->LightData.Intensity = m_Scene.Lights[i].Intensity;
                    lightEntity->LightData.Type      = m_Scene.Lights[i].Type;
                    lightEntity->LightData.Radius    = m_Scene.Lights[i].Radius;

                    // For directional lights, convert direction back to rotation quaternion
                    // (Position is not used for directional lights)
                    if (m_Scene.Lights[i].Type < 0.5f) {
                        glm::vec3 desiredDirection = glm::normalize(m_Scene.Lights[i].Direction);
                        glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
                        glm::vec3 axis             = glm::cross(defaultDirection, desiredDirection);
                        float dot                  = glm::dot(defaultDirection, desiredDirection);

                        if (glm::length(axis) > 0.001f) {  // Not parallel
                            float angle                          = glm::acos(glm::clamp(dot, -1.0f, 1.0f));
                            lightEntity->LocalTransform.Rotation = glm::angleAxis(angle, glm::normalize(axis));
                        }
                        else if (dot < 0.0f) {  // Directly opposite
                            lightEntity->LocalTransform.Rotation
                                    = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
                        }
                        else {
                            lightEntity->LocalTransform.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
                        }
                    }
                    // For point lights: position is determined by hierarchy structure, not synced here
                    // To move a point light, edit the lighting_group position or light local position in hierarchy
                }
            }
        }
    }

    void ClientLayer::SaveScene()
    {
        // Hierarchy is already synced to flat arrays every frame in OnUpdate()
        // Just save to YAML
        std::string scenePath = SCENES_DIR + m_CurrentScene + ".yaml";
        SceneLoader::SaveToYAMLWithHierarchy(scenePath, m_Scene, m_SceneRoot);
        WL_INFO_TAG("Client", "Scene saved to: {}", scenePath);

        // Reload scene from file to verify save
        LoadScene(m_CurrentScene);
    }

    void ClientLayer::ImGuiRenderSceneHierarchy()
    {
        ImGui::Begin("Scene");

        ImGui::Text("Current Scene: %s", m_CurrentScene.c_str());

        if (ImGui::BeginCombo("##SceneSelector", m_SelectedScene.c_str())) {
            for (const auto& sceneName : m_AvailableScenes) {
                if (ImGui::Selectable(sceneName.c_str(), m_SelectedScene == sceneName)) {
                    m_SelectedScene = sceneName;
                    LoadScene(sceneName);
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Save Scene", ImVec2(-1, 0))) { SaveScene(); }

        ImGui::Separator();
        for (auto& child : m_SceneRoot.Children) { ImGuiRenderEntity(child, glm::mat4(1.0f)); }

        ImGui::End();
    }

    void ClientLayer::ImGuiRenderEntity(SceneEntity& entity, const glm::mat4& parentWorldTransform)
    {
        // Compute world transform for this entity
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorldTransform);

        // Unique ID for this entity using its address
        auto idStr = std::to_string((uintptr_t) &entity);

        // Create tree node for this entity (collapsed by default)
        // Use entity pointer as unique ID so tree state persists during name edits
        // Printf-style formatting keeps label separate from ID for stability
        bool isOpen = ImGui::TreeNodeEx((void*) (uintptr_t) &entity, 0, "%s", entity.Name.c_str());

        if (isOpen) {
            // Editable entity name
            std::string nameLabel = "Name##" + idStr;
            ImGui::InputText(nameLabel.c_str(), &entity.Name);

            ImGui::Separator();

            // Transform controls (collapsed by default)
            if (ImGui::TreeNodeEx(("Transform##" + idStr).c_str())) {
                ImGuiRenderTransformControls(entity.LocalTransform, idStr);
                ImGui::TreePop();
            }

            // Entity-specific properties (collapsed by default)
            if (ImGui::TreeNodeEx(("Properties##" + idStr).c_str())) {
                ImGuiRenderEntityProperties(entity);
                ImGui::TreePop();
            }

            // Children section
            if (!entity.Children.empty()) {
                ImGui::Separator();
                ImGui::Text("Children:");
                ImGui::Indent();
                for (auto& child : entity.Children) { ImGuiRenderEntity(child, worldTransform); }
                ImGui::Unindent();
            }

            ImGui::TreePop();
        }
    }

    void ClientLayer::ImGuiRenderTransformControls(Transform& localTransform, const std::string& id)
    {
        // Position drag controls
        glm::vec3 pos = localTransform.Position;
        if (ImGui::DragFloat3(("Position##" + id).c_str(), &pos.x, 0.1f, -100.0f, 100.0f)) {
            localTransform.Position = pos;
        }

        // Rotation - display as direction for intuitive control
        glm::vec3 direction = glm::normalize(
                glm::vec3(glm::mat4_cast(localTransform.Rotation) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        if (ImGui::DragFloat3(("Direction##" + id).c_str(), &direction.x, 0.01f, -1.0f, 1.0f)) {
            direction                  = glm::normalize(direction);
            glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 axis             = glm::cross(defaultDirection, direction);
            float dot                  = glm::dot(defaultDirection, direction);

            if (glm::length(axis) > 0.001f) {
                float angle             = glm::acos(glm::clamp(dot, -1.0f, 1.0f));
                localTransform.Rotation = glm::angleAxis(angle, glm::normalize(axis));
            }
            else if (dot < 0.0f) {
                localTransform.Rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            }
            else {
                localTransform.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        // Scale drag controls
        glm::vec3 scale = localTransform.Scale;
        if (ImGui::DragFloat3(("Scale##" + id).c_str(), &scale.x, 0.1f, 0.01f, 100.0f)) {
            localTransform.Scale = scale;
        }
    }

    void ClientLayer::ImGuiRenderEntityProperties(SceneEntity& entity)
    {
        auto idStr = std::to_string((uintptr_t) &entity);

        switch (entity.Type) {
            case EntityType::Light: {
                // Light type
                bool isDirectional       = entity.LightData.Type < 0.5f;
                const char* lightTypes[] = { "Directional", "Point" };
                int selectedType         = isDirectional ? 0 : 1;
                if (ImGui::Combo(
                            ("Light Type##" + idStr).c_str(), &selectedType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
                    entity.LightData.Type = selectedType == 0 ? 0.0f : 1.0f;
                }

                // Color picker
                ImGui::ColorEdit3(("Color##" + idStr).c_str(), &entity.LightData.Color[0]);

                // Intensity control
                ImGui::DragFloat(("Intensity##" + idStr).c_str(), &entity.LightData.Intensity, 0.01f, 0.0f, 10.0f);

                // Type-specific properties
                if (!isDirectional) {
                    ImGui::DragFloat(("Radius##" + idStr).c_str(), &entity.LightData.Radius, 0.1f, 0.1f, 100.0f);
                }
                break;
            }

            case EntityType::Mesh: {
                // Material index
                int matIdx = entity.MeshData.MaterialIndex;
                if (ImGui::DragInt(("Material Index##" + idStr).c_str(), &matIdx, 1.0f, 0,
                            (int) m_Scene.Materials.size() - 1)) {
                    entity.MeshData.MaterialIndex = matIdx;
                }

                // Texture selector for assigned material
                if (matIdx >= 0 && matIdx < (int) m_Scene.Materials.size()) {
                    Material& mat = m_Scene.Materials[matIdx];

                    ImGui::Separator();
                    ImGui::Text("Material: %s", mat.Name.c_str());

                    // Tiling factor
                    if (ImGui::DragFloat(("Tiling##" + idStr).c_str(), &mat.Tiling, 0.1f, 0.01f, 100.0f)) {
                        m_Renderer.InvalidateScene();
                    }

                    ImGui::Text("Texture");

                    // Display current texture path
                    ImGui::Text("Current: %s", mat.TextureFilename.empty() ? "(none)" : mat.TextureFilename.c_str());

                    // Texture combo box (textures discovered at startup)
                    if (ImGui::BeginCombo(("Texture##" + idStr).c_str(),
                                mat.TextureFilename.empty() ? "(none)" : mat.TextureFilename.c_str())) {
                        if (ImGui::Selectable("(none)", mat.TextureFilename.empty())) {
                            mat.TextureFilename.clear();
                            m_Renderer.InvalidateScene();
                        }
                        for (const auto& textureName : m_AvailableTextures) {
                            if (ImGui::Selectable(textureName.c_str(), mat.TextureFilename == textureName)) {
                                mat.TextureFilename = textureName;
                                m_Renderer.InvalidateScene();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::BeginCombo(("Mesh##" + idStr).c_str(), entity.MeshData.Filename.c_str())) {
                    for (const auto& modelName : m_AvailableModels) {
                        if (ImGui::Selectable(modelName.c_str(), entity.MeshData.Filename == modelName)) {
                            entity.MeshData.Filename = modelName;

                            // Load new mesh data and update the flat scene mesh
                            Mesh newMesh = MeshLoader::LoadOBJ(modelName);
                            auto it      = m_HierarchyMapping.EntityToMeshIdx.find(&entity);
                            if (it != m_HierarchyMapping.EntityToMeshIdx.end()) {
                                uint32_t meshIdx = it->second;
                                if (meshIdx < m_Scene.StaticMeshes.size()) {
                                    // Keep transform and material index, update geometry
                                    newMesh.Transform             = m_Scene.StaticMeshes[meshIdx].Transform;
                                    newMesh.MaterialIndex         = m_Scene.StaticMeshes[meshIdx].MaterialIndex;
                                    m_Scene.StaticMeshes[meshIdx] = newMesh;
                                }
                            }

                            m_Renderer.InvalidateScene();
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }

            case EntityType::Empty: {
                ImGui::Text("Empty group");
                break;
            }

            case EntityType::Camera: {
                ImGui::Text("Camera (not yet editable)");
                break;
            }

            default: break;
        }

        ImGui::Separator();
        ImGui::Text("Script");
        if (ImGui::BeginCombo(
                    ("Script##" + idStr).c_str(), entity.ScriptPath.empty() ? "(none)" : entity.ScriptPath.c_str())) {
            if (ImGui::Selectable("(none)", entity.ScriptPath.empty())) {
                entity.ScriptPath.clear();
                entity.ScriptInitialized = false;
            }
            for (const auto& scriptName : m_AvailableScripts) {
                if (ImGui::Selectable(scriptName.c_str(), entity.ScriptPath == scriptName)) {
                    entity.ScriptPath        = scriptName;
                    entity.ScriptInitialized = false;  // Force reload
                }
            }
            ImGui::EndCombo();
        }
    }

    void ClientLayer::FlattenHierarchyToScene(const SceneEntity& entity, const glm::mat4& parentWorld)
    {
        // Compute world transform for this entity
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorld);

        // Process based on entity type
        if (entity.Type == EntityType::Mesh) {
            // Find this entity in the mapping
            auto it = m_HierarchyMapping.EntityToMeshIdx.find(const_cast<SceneEntity*>(&entity));
            if (it != m_HierarchyMapping.EntityToMeshIdx.end()) {
                uint32_t meshIdx = it->second;
                if (meshIdx < m_Scene.StaticMeshes.size()) {
                    // Update mesh transform and properties
                    m_Scene.StaticMeshes[meshIdx].Transform     = worldTransform;
                    m_Scene.StaticMeshes[meshIdx].MaterialIndex = entity.MeshData.MaterialIndex;
                }
            }
        }
        else if (entity.Type == EntityType::Light) {
            // Find this entity in the mapping
            auto it = m_HierarchyMapping.EntityToLightIdx.find(const_cast<SceneEntity*>(&entity));
            if (it != m_HierarchyMapping.EntityToLightIdx.end()) {
                uint32_t lightIdx = it->second;
                if (lightIdx < m_Scene.Lights.size()) {
                    // Update light properties
                    Light& light    = m_Scene.Lights[lightIdx];
                    light.Color     = entity.LightData.Color;
                    light.Intensity = entity.LightData.Intensity;
                    light.Type      = entity.LightData.Type;
                    light.Radius    = entity.LightData.Radius;

                    // Compute world position and direction
                    light.Position             = glm::vec3(worldTransform[3]);
                    glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
                    light.Direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(defaultDirection, 0.0f)));
                }
            }
        }
        // Empty and Camera types don't add to scene, just pass through

        // Recursively process children
        for (const auto& child : entity.Children) { FlattenHierarchyToScene(child, worldTransform); }
    }

    void ClientLayer::ImGuiRenderChatPanel()
    {
        if (ImGui::Begin("Chat", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::BeginChild("ChatHistory", ImVec2(400, 200), true);
            m_ChatMutex.lock();
            for (const auto& msg : m_ChatHistory) {
                ImGui::TextWrapped("%s: %s", msg.Username.c_str(), msg.Message.c_str());
            }
            m_ChatMutex.unlock();

            // Auto-scroll to bottom
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) { ImGui::SetScrollHereY(1.0f); }
            ImGui::EndChild();

            // Input field
            ImGui::Spacing();
            bool sendMessage = false;
            if (ImGui::InputText("##ChatInput", &m_ChatInputBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
                sendMessage = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Send", ImVec2(80, 0))) { sendMessage = true; }

            if (sendMessage && !m_ChatInputBuffer.empty()) {
                SendChatMessage(m_ChatInputBuffer);
                m_ChatInputBuffer.clear();
                ImGui::SetKeyboardFocusHere(-1);  // Return focus to input
            }
        }
        ImGui::End();
    }

    void ClientLayer::SendChatMessage(const std::string& message)
    {
        if (m_Client.GetConnectionStatus() != Walnut::Client::ConnectionStatus::Connected) {
            WL_WARN_TAG("Client", "Cannot send message: not connected to server");
            return;
        }

        // Format username with player ID
        std::string usernameWithId = m_UserInfo.Username + " [" + std::to_string(m_PlayerID) + "]";

        Walnut::BufferStreamWriter stream(s_ScratchBuffer);
        stream.WriteRaw(PacketType::Message);
        stream.WriteString(usernameWithId);
        stream.WriteString(message);

        m_Client.SendBuffer(stream.GetBuffer());
        // Message will appear in chat when server broadcasts it back to all clients
    }
}  // namespace Vlkrt