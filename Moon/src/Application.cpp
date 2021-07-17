#include "mnpch.h"
#include "Application.h"

#include "DDSTextureLoader.h"
#include <iostream>
#include <fstream>

using namespace Microsoft::WRL;
using namespace DirectX;

Moon::Application* gApplication = nullptr;

namespace Moon
{
	void Application::Init()
	{
		gApplication = this;

		InitMainWindow();

		mRenderDoc = new Moon::RenderDoc();

		InitD3D12();
		InitCommandObjects();
		InitSwapchain();
		InitDescriptorHeaps();
		OnResize();

		DX_CHECK(mCommandList->Reset(mCommandAllocator.Get(), nullptr));
		InitPipeline();
		LoadImages();
		LoadMeshes();
		InitScene();
		mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(mCommandList.Get());

		mCamera = new Camera(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
		mCamera->SetPosition(0.0f, 2.0f, 0.0f);

		isD3D12Initialized = true;
	}

	void Application::Cleanup()
	{
		mQueues->GetGraphicsQueue()->WaitForIdle();
		delete mQueues;

		delete mCamera;

		if (mRenderDoc)
		{
			mRenderDoc->Deinit();
			delete mRenderDoc;
		}
	}

	void Application::Run()
	{
		MSG msg = { 0 };
		mTimer.Reset();

		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				mTimer.Tick();
				if (!mAppPaused)
				{
					UpdateCamera(mTimer);
					Draw();
				}
				else
				{
					Sleep(100);
				}
			}
		}
	}

	void Application::Draw()
	{
		if (mRenderDoc)
			mRenderDoc->Begin();

		mQueues->GetGraphicsQueue()->WaitForFenceCPUBlocking(mCurrentFence);

		//BeginFrame
		DX_CHECK(mCommandAllocator->Reset());
		DX_CHECK(mCommandList->Reset(mCommandAllocator.Get(), nullptr/*mPSO.Get()*/));

		mCurrFrameResource = mFrameResources[mCurrBackBuffer].get();

		//Updating Pass CB
		{
			auto passCB = mCurrFrameResource->PassCB.get();
			mCamera->UpdateViewMatrix();
			XMMATRIX view = mCamera->GetView();
			XMMATRIX proj = mCamera->GetProj();
			XMMATRIX viewProj = XMMatrixMultiply(view, proj);
			PerPassCB cameraData;
			XMStoreFloat4x4(&cameraData.View, XMMatrixTranspose(view));
			XMStoreFloat4x4(&cameraData.Proj, XMMatrixTranspose(proj));
			XMStoreFloat4x4(&cameraData.ViewProj, XMMatrixTranspose(viewProj));
			passCB->CopyData(0, cameraData);
		}

		//Updatin Object CB
		{
			auto currObjectCB = mCurrFrameResource->ObjectCB.get();
			for (auto& e : mAllRitems)
			{
				if (e->NumFramesDirty > 0)
				{
					XMMATRIX world = XMLoadFloat4x4(&e->World);
					XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

					PerObjectCB objConstants;
					XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
					XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

					currObjectCB->CopyData(e->ObjCBIndex, objConstants);
					e->NumFramesDirty--;
				}
			}
		}

		auto currentBackBuffer = mSwapchainBuffer[mCurrBackBuffer].Get();
		auto currentBackBufferView = CD3DX12_CPU_DESCRIPTOR_HANDLE(
			mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			mCurrBackBuffer,
			mRtvDescriptorSize);

		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		FLOAT clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
		mCommandList->ClearRenderTargetView(currentBackBufferView, clearColor, 0, nullptr);
		mCommandList->ClearDepthStencilView(mDsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		mCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &mDsvHeap->GetCPUDescriptorHandleForHeapStart());

		//BeginDraw
		RENDER_PASS("Mesh")
		{
			DrawMesh();
		}

		//EndFrame
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

		mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(mCommandList.Get());

		// swap the back and front buffers
		DX_CHECK(mSwapchain->Present(1, 0));
		mCurrBackBuffer = (mCurrBackBuffer + 1) % BACKBUFFER_COUNT;

		if (mRenderDoc)
			mRenderDoc->End();
	}

	void Application::DrawMesh()
	{
		mCommandList->SetPipelineState(mMeshPSO.Get());
		ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
		mCommandList->SetGraphicsRootSignature(mMeshRootSig.Get());
		auto passCB = mCurrFrameResource->PassCB->Resource();
		mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

		UINT objCBByteSize = CalcConstantBufferByteSize(sizeof(PerObjectCB));
		auto objectCB = mCurrFrameResource->ObjectCB->Resource();

		// For each render item...
		for (size_t i = 0; i < mOpaqueRitems.size(); ++i)
		{
			auto ri = mOpaqueRitems[i];
			CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
			tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

			mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);
			mCommandList->SetGraphicsRootDescriptorTable(0, tex);
			mCommandList->SetGraphicsRootConstantBufferView(1, objCBAddress);
			mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		}
	}

	void Application::UpdateCamera(const Timer& timer)
	{
		const float dt = timer.DeltaTime();
		float movementSpeed = 10.0f;

		if (GetAsyncKeyState(VK_LSHIFT) & 0x8000)
			movementSpeed *= 5;

		if (GetAsyncKeyState('W') & 0x8000)
			mCamera->Walk(movementSpeed * dt);

		if (GetAsyncKeyState('S') & 0x8000)
			mCamera->Walk(-movementSpeed * dt);

		if (GetAsyncKeyState('A') & 0x8000)
			mCamera->Strafe(-movementSpeed * dt);

		if (GetAsyncKeyState('D') & 0x8000)
			mCamera->Strafe(movementSpeed * dt);

		mCamera->UpdateViewMatrix();
	}

	void Application::InitMainWindow()
	{
		WNDCLASS wc;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = MsgProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = mhAppInst;
		wc.hIcon = LoadIcon(0, IDI_APPLICATION);
		wc.hCursor = LoadCursor(0, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
		wc.lpszMenuName = 0;
		wc.lpszClassName = L"MainWnd";

		if (!RegisterClass(&wc))
		{
			MessageBox(0, L"RegisterClass Failed.", 0, 0);
		}

		mRect = { 0, 0, mClientWidth, mClientHeight };
		AdjustWindowRect(&mRect, WS_OVERLAPPEDWINDOW, false);
		int width = mRect.right - mRect.left;
		int height = mRect.bottom - mRect.top;

		mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(),
			WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0);
		if (!mhMainWnd)
		{
			MessageBox(0, L"CreateWindow Failed.", 0, 0);
		}

		ShowWindow(mhMainWnd, SW_SHOW);
		UpdateWindow(mhMainWnd);
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
	}

	void Application::InitCommandObjects()
	{
		mQueues = new Moon::CommandQueueManager(mDevice.Get());

		DX_CHECK(mDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mCommandAllocator.GetAddressOf())));

		DX_CHECK(mDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mCommandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(mCommandList.GetAddressOf())));
		mCommandList->Close();
	}

	void Application::InitSwapchain()
	{
		mSwapchain.Reset();

		DXGI_SWAP_CHAIN_DESC sd;
		sd.BufferDesc.Width = mClientHeight;
		sd.BufferDesc.Height = mClientWidth;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferDesc.Format = mBackBufferFormat;
		sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = BACKBUFFER_COUNT;
		sd.OutputWindow = mhMainWnd;
		sd.Windowed = true;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		DX_CHECK(mFactory->CreateSwapChain(
			mQueues->GetGraphicsQueue()->GetCommandQueue(),
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
		DX_CHECK(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;
		DX_CHECK(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX_CHECK(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));
	}

	void Application::OnResize()
	{
		assert(mDevice);
		assert(mSwapchain);
		assert(mCommandAllocator);

		mQueues->GetGraphicsQueue()->WaitForIdle();

		DX_CHECK(mCommandList->Reset(mCommandAllocator.Get(), nullptr));

		for (int i = 0; i < BACKBUFFER_COUNT; ++i)
			mSwapchainBuffer[i].Reset();
		mDepthStencilBuffer.Reset();

		DX_CHECK(mSwapchain->ResizeBuffers(
			BACKBUFFER_COUNT,
			mClientWidth, mClientHeight,
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
		depthStencilDesc.Width = mClientWidth;
		depthStencilDesc.Height = mClientHeight;
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

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

		mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(mCommandList.Get());

		mScreenViewport.TopLeftX = 0;
		mScreenViewport.TopLeftY = 0;
		mScreenViewport.Width = static_cast<float>(mClientWidth);
		mScreenViewport.Height = static_cast<float>(mClientHeight);
		mScreenViewport.MinDepth = 0.0f;
		mScreenViewport.MaxDepth = 1.0f;

		mScissorRect = { 0, 0, mClientWidth, mClientHeight };
	}

	void Application::InitPipeline()
	{
		// Mesh shaders
		ComPtr<ID3DBlob> meshVsShader = LoadShaderBinary(L"../shaders/mesh.vs.cso");
		ComPtr<ID3DBlob> meshPsShader = LoadShaderBinary(L"../shaders/mesh.ps.cso");

		// Mesh Input Layout
		D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		//Mesh Root Signature
		CD3DX12_DESCRIPTOR_RANGE texTable;
		texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[3];// Perfomance TIP: Order from most frequent to least frequent.
		slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[1].InitAsConstantBufferView(0);
		slotRootParameter[2].InitAsConstantBufferView(1);

		auto staticSamplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter,
			(UINT)staticSamplers.size(), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}
		DX_CHECK(hr);
		DX_CHECK(mDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mMeshRootSig.GetAddressOf())));

		// Mesh PSO
		D3D12_GRAPHICS_PIPELINE_STATE_DESC meshPsoDesc;
		ZeroMemory(&meshPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		meshPsoDesc.InputLayout = { inputLayout, (UINT)std::size(inputLayout) };
		meshPsoDesc.pRootSignature = mMeshRootSig.Get();
		meshPsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(meshVsShader->GetBufferPointer()),
			meshVsShader->GetBufferSize()
		};
		meshPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(meshPsShader->GetBufferPointer()),
			meshPsShader->GetBufferSize()
		};
		meshPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		meshPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		meshPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		meshPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		meshPsoDesc.SampleMask = UINT_MAX;
		meshPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		meshPsoDesc.NumRenderTargets = 1;
		meshPsoDesc.RTVFormats[0] = mBackBufferFormat;
		meshPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		meshPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		meshPsoDesc.DSVFormat = mDepthStencilFormat;
		DX_CHECK(mDevice->CreateGraphicsPipelineState(&meshPsoDesc, IID_PPV_ARGS(&mMeshPSO)));
	}

	void Application::LoadImages()
	{
		auto lostEmpire = std::make_unique<Texture>();
		lostEmpire->Name = "lostEmpireTex";
		lostEmpire->Filename = L"../assets/lost-empire/lost_empire-RGBA.dds";
		DX_CHECK(DirectX::CreateDDSTextureFromFile12(mDevice.Get(),
			mCommandList.Get(), lostEmpire->Filename.c_str(),
			lostEmpire->Resource, lostEmpire->UploadHeap));

		mTextures[lostEmpire->Name] = std::move(lostEmpire);
		auto lostEmpireTex = mTextures["lostEmpireTex"]->Resource;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = lostEmpireTex->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = lostEmpireTex->GetDesc().MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		mDevice->CreateShaderResourceView(lostEmpireTex.Get(), &srvDesc, hDescriptor);
	}

	void Application::LoadMeshes()
	{
		ObjMesh lostEmpire{};
		lostEmpire.LoadFromObjFile("../assets/lost-empire/lost_empire.obj");

		SubmeshGeometry boxSubmesh;
		boxSubmesh.IndexCount = (UINT)lostEmpire.indices.size();
		boxSubmesh.StartIndexLocation = 0;
		boxSubmesh.BaseVertexLocation = 0;

		std::vector<std::uint32_t> indices = lostEmpire.indices;
		std::vector<Vertex> vertices(lostEmpire.vertices.size());
		for (size_t i = 0; i < lostEmpire.vertices.size(); ++i)
		{
			vertices[i].position = lostEmpire.vertices[i].position;
			vertices[i].normal = lostEmpire.vertices[i].normal;
			vertices[i].uv = lostEmpire.vertices[i].uv;
		}

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "lostEmpire";

		DX_CHECK(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
		DX_CHECK(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = CreateDefaultBuffer(mDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
		geo->IndexBufferGPU = CreateDefaultBuffer(mDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibByteSize;
		geo->DrawArgs["lostEmpire"] = boxSubmesh;

		mGeometries[geo->Name] = std::move(geo);
	}

	void Application::InitScene()
	{
		auto lostEmpire = std::make_unique<Material>();
		lostEmpire->Name = "lostEmpire";
		lostEmpire->MatCBIndex = 0;
		lostEmpire->DiffuseSrvHeapIndex = 0;
		lostEmpire->DiffuseAlbedo = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		lostEmpire->FresnelR0 = DirectX::XMFLOAT3(0.05f, 0.05f, 0.05f);
		lostEmpire->Roughness = 0.2f;
		mMaterials["lostEmpire"] = std::move(lostEmpire);

		auto leRitem = std::make_unique<RenderItem>();
		leRitem->ObjCBIndex = 0;
		leRitem->Mat = mMaterials["lostEmpire"].get();
		leRitem->Geo = mGeometries["lostEmpire"].get();
		leRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leRitem->IndexCount = leRitem->Geo->DrawArgs["lostEmpire"].IndexCount;
		leRitem->StartIndexLocation = leRitem->Geo->DrawArgs["lostEmpire"].StartIndexLocation;
		leRitem->BaseVertexLocation = leRitem->Geo->DrawArgs["lostEmpire"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leRitem));

		for (auto& e : mAllRitems)
			mOpaqueRitems.push_back(e.get());

		for (int i = 0; i < BACKBUFFER_COUNT; ++i)
		{
			mFrameResources.push_back(std::make_unique<FrameResource>(mDevice.Get(), 1, (UINT)mAllRitems.size()));
		}
	}

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> Application::GetStaticSamplers()
	{
		const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
			0, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			1, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
			2, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
			3, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
			4, // shaderRegister
			D3D12_FILTER_ANISOTROPIC, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
			0.0f,                             // mipLODBias
			8);                               // maxAnisotropy

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
			5, // shaderRegister
			D3D12_FILTER_ANISOTROPIC, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
			0.0f,                              // mipLODBias
			8);                                // maxAnisotropy

		return {
			pointWrap, pointClamp,
			linearWrap, linearClamp,
			anisotropicWrap, anisotropicClamp };
	}

	Microsoft::WRL::ComPtr<ID3DBlob> Application::LoadShaderBinary(const std::wstring& filename)
	{
		std::ifstream fin(filename, std::ios::binary);
		if (!fin)
			throw std::runtime_error("Unable to open shader file..");
		fin.seekg(0, std::ios_base::end);
		std::ifstream::pos_type size = (int)fin.tellg();
		fin.seekg(0, std::ios_base::beg);

		ComPtr<ID3DBlob> blob;
		DX_CHECK(D3DCreateBlob(size, blob.GetAddressOf()));

		fin.read((char*)blob->GetBufferPointer(), size);
		fin.close();

		return blob;
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> Application::CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
	{
		ComPtr<ID3D12Resource> defaultBuffer;

		DX_CHECK(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

		DX_CHECK(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = initData;
		subResourceData.RowPitch = byteSize;
		subResourceData.SlicePitch = subResourceData.RowPitch;

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
		UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

		return defaultBuffer;
	}

	void Application::SetFullscreen(bool fullscreen)
	{
		if (mFullscreenState != fullscreen)
		{
			mFullscreenState = fullscreen;
			if (mFullscreenState) // Switching to fullscreen.
			{
				// Store the current window dimensions so they can be restored 
				// when switching out of fullscreen state.
				::GetWindowRect(mhMainWnd, &mRect);

				mPreviousClientWidth = mClientWidth;
				mPreviousClientHeight = mClientHeight;

				// Set the window style to a borderless window so the client area fills
				// the entire screen.
				UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
				::SetWindowLongW(mhMainWnd, GWL_STYLE, windowStyle);

				// Query the name of the nearest display device for the window.
				// This is required to set the fullscreen dimensions of the window
				// when using a multi-monitor setup.
				HMONITOR hMonitor = ::MonitorFromWindow(mhMainWnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFOEX monitorInfo = {};
				monitorInfo.cbSize = sizeof(MONITORINFOEX);
				::GetMonitorInfo(hMonitor, &monitorInfo);
				::SetWindowPos(mhMainWnd, HWND_TOPMOST,
					monitorInfo.rcMonitor.left,
					monitorInfo.rcMonitor.top,
					monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
					monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
					SWP_FRAMECHANGED | SWP_NOACTIVATE);
				::ShowWindow(mhMainWnd, SW_MAXIMIZE);

				/*MN_ENGINE_INFO("Fullscreen Rect-> L:{0} R:{1} T:{2} B:{3}",
					monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.right,
					monitorInfo.rcMonitor.top, monitorInfo.rcMonitor.bottom);*/

				//Resize Swapchain to match fullscreen
				mClientWidth = monitorInfo.rcMonitor.right;
				mClientHeight = monitorInfo.rcMonitor.bottom;
				OnResize();
			}
			else
			{
				// Restore all the window decorators.
				::SetWindowLong(mhMainWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
				::SetWindowPos(mhMainWnd, HWND_NOTOPMOST,
					mRect.left,
					mRect.top,
					mRect.right - mRect.left,
					mRect.bottom - mRect.top,
					SWP_FRAMECHANGED | SWP_NOACTIVATE);
				::ShowWindow(mhMainWnd, SW_NORMAL);

				//Resize Swapchain to match windowed
				mClientWidth = mPreviousClientWidth;
				mClientHeight = mPreviousClientHeight;
				OnResize();
			}
		}
	}

	void Application::OnMouseDown(WPARAM btnState, int x, int y)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;
	}

	void Application::OnMouseUp(WPARAM btnState, int x, int y)
	{
	}

	void Application::OnMouseMove(WPARAM btnState, int x, int y)
	{
		if ((btnState & MK_LBUTTON) != 0)
		{
			float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
			float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

			mCamera->Pitch(dy);
			mCamera->RotateY(dx);
		}
		mLastMousePos.x = static_cast<LONG>(x);
		mLastMousePos.y = static_cast<LONG>(y);
	}

	LRESULT Application::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ACTIVATE:
			if (LOWORD(wParam) == WA_INACTIVE)
			{
				gApplication->mAppPaused = true;
				gApplication->mTimer.Stop();
			}
			else
			{
				gApplication->mAppPaused = false;
				gApplication->mTimer.Start();
			}
			return 0;

		case WM_SIZE:
			// Save the new client area dimensions.
			gApplication->mClientWidth = LOWORD(lParam);
			gApplication->mClientHeight = HIWORD(lParam);
			if (gApplication->isD3D12Initialized && gApplication->mDevice)
			{
				if (wParam == SIZE_MINIMIZED)
				{
					gApplication->mAppPaused = true;
					gApplication->mMinimized = true;
					gApplication->mMaximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					gApplication->mAppPaused = false;
					gApplication->mMinimized = false;
					gApplication->mMaximized = true;
					gApplication->OnResize();
				}
				else if (wParam == SIZE_RESTORED)
				{

					// Restoring from minimized state?
					if (gApplication->mMinimized)
					{
						gApplication->mAppPaused = false;
						gApplication->mMinimized = false;
						gApplication->OnResize();
					}

					// Restoring from maximized state?
					else if (gApplication->mMaximized)
					{
						gApplication->mAppPaused = false;
						gApplication->mMaximized = false;
						gApplication->OnResize();
					}
					else if (gApplication->mResizing)
					{
					}
					else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
					{
						gApplication->OnResize();
					}
				}
			}
			return 0;

		case WM_ENTERSIZEMOVE:
			gApplication->mAppPaused = true;
			gApplication->mResizing = true;
			gApplication->mTimer.Stop();
			return 0;

		case WM_EXITSIZEMOVE:
			gApplication->mAppPaused = false;
			gApplication->mResizing = false;
			gApplication->mTimer.Start();
			gApplication->OnResize();
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_MENUCHAR:
			// Don't beep when we alt-enter.
			return MAKELRESULT(0, MNC_CLOSE);

			// Catch this message so to prevent the window from becoming too small.
		case WM_GETMINMAXINFO:
			((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
			((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
			return 0;

		case WM_LBUTTONDOWN:
			gApplication->OnMouseDown(MK_LBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MBUTTONDOWN:
			gApplication->OnMouseDown(MK_MBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_RBUTTONDOWN:
			gApplication->OnMouseDown(MK_RBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_LBUTTONUP:
			gApplication->OnMouseUp(MK_LBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MBUTTONUP:
			gApplication->OnMouseUp(MK_MBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_RBUTTONUP:
			gApplication->OnMouseUp(MK_RBUTTON, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MOUSEMOVE:
			gApplication->OnMouseMove(wParam, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)));
			return 0;
		case WM_MOUSEWHEEL:
			//OnMouseScroll(0, GET_WHEEL_DELTA_WPARAM(wParam));
			return 0;
		case WM_MOUSEHWHEEL:
			//OnMouseScroll(GET_WHEEL_DELTA_WPARAM(wParam), 0);
			return 0;
		case WM_KEYUP:
			if (wParam == VK_ESCAPE)
			{
				PostQuitMessage(0);
			}
			else if ((int)wParam == VK_F11)
			{
				gApplication->SetFullscreen(!gApplication->mFullscreenState);
			}
			return 0;
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}