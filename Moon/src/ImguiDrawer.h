#pragma once
#include "dx_utils.h"
#include "Event.h"
#include <imgui.h>

namespace Moon
{
	class ImguiDrawer
	{
	public:
		ImguiDrawer(HWND window, Microsoft::WRL::ComPtr<ID3D12Device8> device, DXGI_FORMAT backbufferFormat);
		~ImguiDrawer();

		void DrawMenuBar();
		void BeginDrawImgui(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList, int width, int height);
		void EndDrawImgui(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList);
		void OnEvent(Event& e);

	private:
		bool OnMouseButtonPressedEvent(MouseButtonPressedEvent& e);
		bool OnMouseButtonReleasedEvent(MouseButtonReleasedEvent& e);
		bool OnMouseMovedEvent(MouseMovedEvent& e);
		bool OnMouseScrolledEvent(MouseScrolledEvent& e);
		bool OnKeyPressedEvent(KeyPressedEvent& e);
		bool OnKeyReleasedEvent(KeyReleasedEvent& e);

	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mImguiHeap;
	};
}