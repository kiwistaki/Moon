#pragma once
#include "dx_utils.h"
#include "CommandQueue.h"

class Application
{
public:
	void Init();
	void Cleanup();
	void Draw();
	void Run();

private:
	void InitD3D12();
	void InitCommandObjects();
	void InitSwapchain();
	void InitDescriptorHeaps();
	void OnResize();

private:
	Moon::Direct3DQueueManager* mQueues;

	//D3D12 related stuff
	Microsoft::WRL::ComPtr<IDXGIFactory6> mFactory;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> mAdapter;
	DXGI_ADAPTER_DESC1 mAdapterDesc;
	Microsoft::WRL::ComPtr<ID3D12Device8> mDevice;

	int mCurrBackBuffer = 0;
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapchain;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapchainBuffer[BACKBUFFER_COUNT];
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	struct SDL_Window* mWindow{ nullptr };
	int mWidth = 1600;
	int mHeight = 900;
	bool m4xMsaaState = false;
	UINT m4xMsaaQuality = 0;
	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator[BACKBUFFER_COUNT];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList[BACKBUFFER_COUNT];
	UINT64 mCurrentFence = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
	UINT mRtvDescriptorSize = 0;
	UINT mDsvDescriptorSize = 0;
	UINT mCbvSrvUavDescriptorSize = 0;
};
