project "Vlkrt-Client"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{cfg.buildcfg}"
   staticruntime "off"

   files { "Source/**.h", "Source/**.cpp" }

   includedirs
   {
      "../Vlkrt-Common/Source",

      "../Walnut/vendor/imgui",
      "../Walnut/vendor/glfw/include",
      "../Walnut/vendor/glm",
      "../Walnut/vendor/spdlog/include",
      "../Walnut/vendor/tinyobjloader",

      "../Walnut/Walnut/Source",
      "../Walnut/Walnut/Platform/GUI",

      "%{IncludeDir.VulkanSDK}",

      -- Walnut-Networking
      "../Walnut/Walnut-Modules/Walnut-Networking/Source",
      "../Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/include",
   }

   links
   {
      "Vlkrt-Common"
   }

   targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
   objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

   filter "system:windows"
      systemversion "latest"
      defines { "WL_PLATFORM_WINDOWS" }
      buildoptions { "/utf-8" }

      prebuildcommands
      {
         'cd "Source\\Shaders" && call compile_shaders.bat && cd ..\\..'
      }

      postbuildcommands
      {
         '{COPY} "../%{WalnutNetworkingBinDir}GameNetworkingSockets.dll" "%{cfg.targetdir}"',
         '{COPY} "../%{WalnutNetworkingBinDir}libcrypto-3-x64.dll" "%{cfg.targetdir}"',
      }

   filter { "system:windows", "configurations:Debug" }
      postbuildcommands
      {
         '{COPY} "../%{WalnutNetworkingBinDir}libprotobufd.dll" "%{cfg.targetdir}"',
      }

   filter { "system:windows", "configurations:Release or Dist" }
      postbuildcommands
      {
         '{COPY} "../%{WalnutNetworkingBinDir}libprotobuf.dll" "%{cfg.targetdir}"',
      }

   filter "system:linux"
      libdirs { "../Walnut/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux" }
      links { "GameNetworkingSockets" }

      prebuildcommands
      {
         'cd "Source/Shaders" && bash compile_shaders.sh && cd ../..'
      }

   filter "configurations:Debug"
      defines { "WL_DEBUG" }
      runtime "Debug"
      symbols "On"

   filter "configurations:Release"
      defines { "WL_RELEASE" }
      runtime "Release"
      optimize "On"
      symbols "On"

   filter "configurations:Dist"
      kind "WindowedApp"
      defines { "WL_DIST" }
      runtime "Release"
      optimize "On"
      symbols "Off"