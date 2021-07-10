#pragma once
#include <Windows.h>

namespace Moon
{
	class RenderDoc
	{
	public:
		RenderDoc();
		~RenderDoc();

		void Init();
		void Deinit();

		void Begin();
		void End();

	private:
		HMODULE mModule = nullptr;
		void* mApi = nullptr;
		short mNumCaptures = 0;
		bool mRequestCapture = false;
	};
}
