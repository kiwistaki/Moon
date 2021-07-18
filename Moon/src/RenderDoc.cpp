#include "mnpch.h"
#include "RenderDoc.h"

#include <renderdoc_app.h>
#include <shellapi.h>

namespace Moon
{
	RenderDoc::RenderDoc()
		:mModule(nullptr)
		,mApi(nullptr)
	{
		Init();
	}

	RenderDoc::~RenderDoc()
	{
	}

	void RenderDoc::Init()
	{
		if (::GetModuleHandleW(L"renderdoc.dll") == 0)
		{
			mModule = ::LoadLibraryW(L"C://Program Files//RenderDoc//renderdoc.dll");
			auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mModule, "RENDERDOC_GetAPI"));
			if (getApi)
			{
				RENDERDOC_API_1_4_0* api;
				short result = getApi(eRENDERDOC_API_Version_1_4_0, (void**)&api);
				if (result == 1)
				{
					api->SetCaptureFilePathTemplate("../RenderDoc/Moon");
					// Hide Overlay
					api->MaskOverlayBits(RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None, RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None);

					mApi = api;
				}
			}
		}
	}

	void RenderDoc::Deinit()
	{
		::FreeLibrary(mModule);
		mModule = nullptr;
		mApi = nullptr;
	}
}