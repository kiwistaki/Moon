#include "mnpch.h"
#include "RenderDoc.h"

#include <renderdoc_app.h>
#include <shellapi.h>

namespace Moon
{
	RenderDoc::RenderDoc()
		:mModule(nullptr)
		,mApi(nullptr)
		,mNumCaptures(0)
		,mRequestCapture(false)
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
					//api->SetFocusToggleKeys(nullptr, 0);
					//api->SetCaptureKeys(nullptr, 0);
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

	void RenderDoc::Begin()
	{
		if (mRequestCapture)
		{
			mRequestCapture = false;
			if (mApi)
			{
				auto api = static_cast<RENDERDOC_API_1_4_0*>(mApi);
				if (api->IsFrameCapturing() == 0)
				{
					mNumCaptures = api->GetNumCaptures();
					api->TriggerCapture();
					//MN_ENGINE_INFO("[RenderDoc] Capturing...");
				}
			}
		}
	}

	void RenderDoc::End()
	{
		if (mApi)
		{
			auto api = static_cast<RENDERDOC_API_1_4_0*>(mApi);
			short num = api->GetNumCaptures();
			if (num > mNumCaptures)
			{
				unsigned int pathSize = 255;
				char path[255];
				api->GetCapture(num - 1, path, &pathSize, nullptr);
				//MN_ENGINE_INFO("[RenderDoc] Capture taken.");
				mNumCaptures = num;
				::ShellExecuteA(nullptr, nullptr, path, nullptr, nullptr, SW_SHOWNORMAL);
			}
		}
	}
}