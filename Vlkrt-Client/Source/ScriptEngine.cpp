#include "ScriptEngine.h"
#include "Utils.h"

#include "Walnut/Input/Input.h"
#include "Walnut/Core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>

namespace Vlkrt
{
    std::unique_ptr<sol::state> ScriptEngine::s_LuaState = nullptr;

    void ScriptEngine::Init()
    {
        s_LuaState = std::make_unique<sol::state>();
        s_LuaState->open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::table, sol::lib::os);

        RegisterBindings();
    }

    void ScriptEngine::Shutdown() { s_LuaState.reset(); }

    void ScriptEngine::RegisterBindings()
    {
        auto& lua = *s_LuaState;

        // GLM Bindings
        lua.new_usertype<glm::vec3>(
                "vec3", sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(), "x",
                &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z, sol::meta_function::addition,
                [](const glm::vec3& a, const glm::vec3& b) { return a + b; }, sol::meta_function::subtraction,
                [](const glm::vec3& a, const glm::vec3& b) { return a - b; }, sol::meta_function::multiplication,
                [](const glm::vec3& a, float b) { return a * b; });

        lua.new_usertype<glm::quat>("quat", sol::constructors<glm::quat(), glm::quat(float, float, float, float)>(),
                "w", &glm::quat::w, "x", &glm::quat::x, "y", &glm::quat::y, "z", &glm::quat::z,
                sol::meta_function::multiplication, [](const glm::quat& a, const glm::quat& b) { return a * b; });

        lua["AngleAxis"] = [](float angle, const glm::vec3& axis) { return glm::angleAxis(angle, axis); };

        // Transform Bindings
        lua.new_usertype<Transform>("Transform", "Position", &Transform::Position, "Rotation", &Transform::Rotation,
                "Scale", &Transform::Scale);

        // SceneEntity Bindings
        lua.new_usertype<SceneEntity>("SceneEntity", "Name", &SceneEntity::Name, "Transform",
                &SceneEntity::LocalTransform, "SetTransform", &SceneEntity::SetLocalTransform);

        // Input Bindings (Walnut)
        auto input         = lua["Input"].get_or_create<sol::table>();
        input["IsKeyDown"] = [](int keycode) { return Walnut::Input::IsKeyDown((Walnut::KeyCode) keycode); };
        input["IsMouseButtonDown"]
                = [](int button) { return Walnut::Input::IsMouseButtonDown((Walnut::MouseButton) button); };

        // Global functions
        lua["Log"] = [](const std::string& message) { std::cout << "[LUA]: " << message << std::endl; };
    }

    void ScriptEngine::LoadScript(SceneEntity& entity)
    {
        if (entity.ScriptPath.empty()) return;

        try {
            auto result = s_LuaState->safe_script_file(Vlkrt::SCRIPTS_DIR + entity.ScriptPath);
            if (!result.valid()) {
                sol::error err = result;
                WL_ERROR_TAG("ScriptEngine", "Failed to load script '{}': {}", entity.ScriptPath, err.what());
                return;
            }
            entity.ScriptInitialized = true;
        }
        catch (const std::exception& e) {
            WL_ERROR_TAG("ScriptEngine", "LoadScript exception: {}", e.what());
        }
    }

    void ScriptEngine::CallOnUpdate(SceneEntity& entity, float ts)
    {
        if (!entity.ScriptInitialized) return;

        sol::protected_function onUpdate = (*s_LuaState)["OnUpdate"];
        if (!onUpdate.valid()) return;

        auto result = onUpdate(entity, ts);
        if (!result.valid()) {
            sol::error err = result;
            WL_ERROR_TAG("ScriptEngine", "Script Error in OnUpdate: {}", err.what());
        }
    }
}  // namespace Vlkrt
