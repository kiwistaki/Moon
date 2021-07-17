#pragma once
#include "dx_utils.h"

namespace Moon
{
	class ImguiDrawer
	{
	public:
		ImguiDrawer(HWND window, Microsoft::WRL::ComPtr<ID3D12Device8> device, DXGI_FORMAT backbufferFormat);
		~ImguiDrawer();

		void DrawImgui(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList, int width, int height);

	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mImguiHeap;
	};
}