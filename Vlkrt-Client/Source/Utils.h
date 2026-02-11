#pragma once

namespace Vlkrt
{
    // TODO: Make these configurable via a settings file or environment variables
    //       Also, consider using std::filesystem for path handling to ensure cross-platform compatibility
    //	     Last, these could be organized into a ResourceManager class that handles loading and caching of resources
    //
    // Common resource paths
    static const char* TEXTURES_DIR = "../resources/textures/";
    static const char* MODELS_DIR   = "../resources/obj/";
    static const char* SCENES_DIR   = "../resources/scenes/";
    static const char* SCRIPTS_DIR  = "../resources/scripts/";
    static const char* SHADERS_DIR  = "Source/Shaders/";
}  // namespace Vlkrt