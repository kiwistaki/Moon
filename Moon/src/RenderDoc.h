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

	private:
		HMODULE mModule = nullptr;
		void* mApi = nullptr;
	};
}
