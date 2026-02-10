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
      "../Walnut/vendor/yaml-cpp/include",

      "../Walnut/Walnut/Source",
      "../Walnut/Walnut/Platform/GUI",

      "%{IncludeDir.VulkanSDK}",

      -- Walnut-Networking
      "../Walnut/Walnut-Modules/Walnut-Networking/Source",
      "../Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/include",

      "../vendor/sol2/include",
      "../vendor/luajit/src",
   }

   defines
   {
      "YAML_CPP_STATIC_DEFINE",
      "SOL_ALL_SAFETIES_ON=1"
   }

   links
   {
      "Vlkrt-Common",
      "yaml-cpp",
      "lua51"
   }

   libdirs
   {
      "../vendor/luajit/src"
   }

   targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
   objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

   filter "system:windows"
      systemversion "latest"
      defines { "WL_PLATFORM_WINDOWS" }
      buildoptions { "/utf-8" }

      prebuildcommands
      {
         "pushd ..\\vendor\\luajit\\src && if not exist lua51.lib ( msvcbuild.bat ) && popd"
      }

      postbuildcommands
      {
         '{COPY} "../%{WalnutNetworkingBinDir}GameNetworkingSockets.dll" "%{cfg.targetdir}"',
         '{COPY} "../%{WalnutNetworkingBinDir}libcrypto-3-x64.dll" "%{cfg.targetdir}"',
         '{COPY} "../vendor/luajit/src/lua51.dll" "%{cfg.targetdir}"',
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

      postbuildcommands
      {
         '{COPYDIR} "../../scenes" "%{cfg.targetdir}/scenes"',
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