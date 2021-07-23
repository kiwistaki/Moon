#include "Application.h"
#include "dx_utils.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	try
	{
		Moon::Application engine;
		engine.Init();
		engine.Run();
		engine.Cleanup();
		return 0;
	}
	catch (DxException e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"DX12 Error", MB_OK);
		return -1;
	}
}