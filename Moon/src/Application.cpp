#include "mnpch.h"
#include "Application.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <glm/gtx/transform.hpp>

#include <chrono>
#include <iostream>
#include <fstream>

using namespace Microsoft::WRL;


void Application::Init()
{
	// initialize SDL
	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(0);
	mWindow = SDL_CreateWindow(
		"Vulkaneer",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		mWidth,
		mHeight,
		window_flags
	);

	InitD3D12();
}

void Application::Cleanup()
{
	mQueues->GetGraphicsQueue()->WaitForIdle();
	delete mQueues;

	SDL_DestroyWindow(mWindow);
}

void Application::Run()
{
	SDL_Event e;
	bool bQuit = false;
	std::chrono::time_point<std::chrono::system_clock> start, end;

	start = std::chrono::system_clock::now();
	end = std::chrono::system_clock::now();

	//main loop
	while (!bQuit)
	{
		end = std::chrono::system_clock::now();
		std::chrono::duration<float> elapsed_seconds = end - start;
		float frametime = elapsed_seconds.count() * 1000.f;
		start = std::chrono::system_clock::now();

		while (SDL_PollEvent(&e) != 0)
		{
			bool isESCPressed = e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE;
			if (e.type == SDL_QUIT || isESCPressed) bQuit = true;

			//_camera.process_input_event(&e);
		}
		//_camera.update_camera(frametime);
		Draw();
	}
}

void Application::Draw()
{
	//BeginFrame
	auto cmdList = mCommandList[mCurrBackBuffer];
	auto cmdAllocator = mCommandAllocator[mCurrBackBuffer];

	mQueues->GetGraphicsQueue()->WaitForFenceCPUBlocking(mCurrentFence);

	DX_CHECK(cmdAllocator->Reset());
	DX_CHECK(cmdList->Reset(cmdAllocator.Get(), nullptr/*mPSO.Get()*/));

	auto currentBackBuffer = mSwapchainBuffer[mCurrBackBuffer].Get();
	auto currentBackBufferView = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrBackBuffer,
		mRtvDescriptorSize);

	cmdList->RSSetViewports(1, &mScreenViewport);
	cmdList->RSSetScissorRects(1, &mScissorRect);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentBackBuffer,
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	FLOAT clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
	cmdList->ClearRenderTargetView(currentBackBufferView, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(mDsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &currentBackBufferView, true, &mDsvHeap->GetCPUDescriptorHandleForHeapStart());


	//BeginDraw
	{
	}

	//EndFrame
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentBackBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(cmdList.Get());

	// swap the back and front buffers
	DX_CHECK(mSwapchain->Present(1, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % BACKBUFFER_COUNT;
}

void Application::InitD3D12()
{
#if DX12_ENABLE_DEBUG_LAYER
	ComPtr<ID3D12Debug> debugInterface;
	DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
	debugInterface->EnableDebugLayer();
#endif
	// Create Factory
	UINT createFactoryFlag = 0;
#if DX12_ENABLE_DEBUG_LAYER
	createFactoryFlag = DXGI_CREATE_FACTORY_DEBUG;
#endif
	DX_CHECK(CreateDXGIFactory2(createFactoryFlag, IID_PPV_ARGS(&mFactory)));
	// Enumerate adapters
	Microsoft::WRL::ComPtr<IDXGIAdapter1> candidateAdapter;
	for (uint32_t i = 0; mFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidateAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 adapterDesc;
		candidateAdapter->GetDesc1(&adapterDesc);
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
			SUCCEEDED(D3D12CreateDevice(candidateAdapter.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr)))
		{
			mAdapterDesc = adapterDesc;
			DX_CHECK(candidateAdapter.As(&mAdapter));
			break;
		}
	}
	// Create device
	DX_CHECK(D3D12CreateDevice(mAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)));
	// Enable debug messages in debug mode.
#if DX12_ENABLE_DEBUG_LAYER
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(mDevice.As(&infoQueue)))
	{
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
		//D3D12_MESSAGE_CATEGORY Categories[] = {};
		//D3D12_MESSAGE_ID DenyIds[] = {};
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};
		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		DX_CHECK(infoQueue->PushStorageFilter(&NewFilter));
	}
#endif

	mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	DX_CHECK(mDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

	InitCommandObjects();
	InitSwapchain();
	InitDescriptorHeaps();
	OnResize();
}

void Application::InitCommandObjects()
{
	mQueues = new Moon::Direct3DQueueManager(mDevice.Get());

	for (int i = 0; i < BACKBUFFER_COUNT; ++i)
	{
		DX_CHECK(mDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mCommandAllocator[i].GetAddressOf())));

		DX_CHECK(mDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mCommandAllocator[i].Get(),
			nullptr,
			IID_PPV_ARGS(mCommandList[i].GetAddressOf())));
	}
	mCommandList[0]->Close();
	mCommandList[1]->Close();
}

void Application::InitSwapchain()
{
	mSwapchain.Reset();

	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(mWindow, &wmInfo);
	HWND hwnd = wmInfo.info.win.window;

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mHeight;
	sd.BufferDesc.Height = mWidth;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = BACKBUFFER_COUNT;
	sd.OutputWindow = hwnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	DX_CHECK(mFactory->CreateSwapChain(
		mQueues->GetGraphicsQueue()->GetCommandQueue(),//mGraphicsQueue.Get(),
		&sd,
		mSwapchain.GetAddressOf()));
}

void Application::InitDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = BACKBUFFER_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	DX_CHECK(mDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));


	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	DX_CHECK(mDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void Application::OnResize()
{
	assert(mDevice);
	assert(mSwapchain);
	assert(mCommandAllocator);

	mQueues->GetGraphicsQueue()->WaitForIdle();

	DX_CHECK(mCommandList[0]->Reset(mCommandAllocator[0].Get(), nullptr));

	for (int i = 0; i < BACKBUFFER_COUNT; ++i)
		mSwapchainBuffer[i].Reset();
	mDepthStencilBuffer.Reset();

	DX_CHECK(mSwapchain->ResizeBuffers(
		BACKBUFFER_COUNT,
		mWidth, mHeight,
		mBackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	mCurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < BACKBUFFER_COUNT; i++)
	{
		DX_CHECK(mSwapchain->GetBuffer(i, IID_PPV_ARGS(&mSwapchainBuffer[i])));
		mDevice->CreateRenderTargetView(mSwapchainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}


	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mWidth;
	depthStencilDesc.Height = mHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	DX_CHECK(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	mDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

	mCommandList[0]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(mCommandList[0].Get());

	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width = static_cast<float>(mWidth);
	mScreenViewport.Height = static_cast<float>(mHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	mScissorRect = { 0, 0, mWidth, mHeight };
}
