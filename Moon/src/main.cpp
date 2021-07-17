#include "Application.h"
#include "dx_utils.h"

int main(int argc, char** argv)
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
	}
}