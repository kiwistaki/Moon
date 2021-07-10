workspace "Moon"
	configurations { "Debug", "Release" }
	architecture "x86_64"
	flags { "MultiProcessorCompile" }
	startproject "Moon"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"


IncludeDir = {}
IncludeDir["assimp"] = "libs/assimp/include"
IncludeDir["glm"] = "libs/glm/"
IncludeDir["imgui"] = "libs/imgui/"
IncludeDir["RenderDoc"] = "libs/RenderDoc/"
IncludeDir["sdl2"] = "libs/sdl2/include"
IncludeDir["tinyobjloader"] = "libs/tinyobjloader/"

LibraryDir = {}
LibraryDir["assimp"] = "../libs/assimp/bin/Release/assimp-vc142-mt.lib"
LibraryDir["sdl2"] = "../libs/sdl2/lib/x64/sdl2.lib"

project "Moon"
	location "Moon"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("obj/" .. outputdir .. "/%{prj.name}")

	pchheader "mnpch.h"
	pchsource "Moon/src/mnpch.cpp"

	files
	{
		"%{prj.name}/**.h",
		"%{prj.name}/**.cpp",
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
	}

	includedirs
	{
		"%{prj.name}/",
		"%{IncludeDir.assimp}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.imgui}",
		"%{IncludeDir.RenderDoc}",
		"%{IncludeDir.sdl2}",
		"%{IncludeDir.tinyobjloader}",
	}

	links 
	{
		"%{LibraryDir.assimp}",
		"%{LibraryDir.sdl2}",
	}

	postbuildcommands 
	{
		'{COPY} "../libs/assimp/bin/Release/assimp-vc142-mt.dll" "%{cfg.targetdir}"',
		'{COPY} "../libs/sdl2/lib/x64/SDL2.dll" "%{cfg.targetdir}"',
	}

	filter "files:Moon/src/main.cpp or Moon/src/DirectXTex/**.cpp or files:Moon/src/D3D12MemoryAllocator/**.cpp"
   		flags { "NoPCH" }

	filter "system:windows"
		systemversion "latest"

		defines
		{
		}
	
	filter "configurations:Debug"
		defines {"DEBUG", "_DEBUG"}
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines {"NDEBUG","_RELEASE"}
		runtime "Release"
		optimize "on"

