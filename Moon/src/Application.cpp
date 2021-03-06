#include "mnpch.h"
#include "Application.h"

#include "DDSTextureLoader.h"

#include <iostream>
#include <fstream>

using namespace Microsoft::WRL;
using namespace DirectX;

namespace Moon
{
	Application* Application::gApplication = nullptr;
	
	void Application::Init()
	{
		gApplication = this;
		mWindow = new Window();
		mRenderDoc = new RenderDoc();

		InitD3D12();
		InitCommandObjects();
		InitQuery();
		InitSwapchain();
		InitDescriptorHeaps();
		Resize();

		DX_CHECK(mCommandList[0]->Reset(mCommandAllocator[0].Get(), nullptr));
		InitPipeline();
		LoadImages();
		LoadMeshes();
		InitScene();
		mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(mCommandList[0].Get());

		mCamera = new Camera(static_cast<float>(mWindow->GetWidth()), static_cast<float>(mWindow->GetHeight()));
		mCamera->SetPosition(-5.0f, 12.0f, 2.0f);
		mImguiDrawer = new ImguiDrawer(mWindow->GetWindowHandle(), mDevice, mBackBufferFormat);
		mFrameStats.Init();
		isD3D12Initialized = true;
	}

	void Application::Cleanup()
	{
		mQueues->GetGraphicsQueue()->WaitForIdle();
		delete mQueues;
		delete mImguiDrawer;
		delete mCamera;
		mFrameStats.Deinit();

		if (mRenderDoc)
		{
			mRenderDoc->Deinit();
			delete mRenderDoc;
		}
		delete mWindow;
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
				mTotalCpuTimeMS += mTimer.DeltaTime()*1000;
				if (!mAppPaused)
				{
					mCamera->Update(mTimer.DeltaTime());
					Draw();
					GetQueryResult();
					mFrameNumber++;
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
		mQueues->GetGraphicsQueue()->WaitForFenceCPUBlocking(mCurrentFence);

		auto cmdList = mCommandList[mCurrBackBuffer];
		auto cmdAllocator = mCommandAllocator[mCurrBackBuffer];

		//BeginFrame
		DX_CHECK(cmdAllocator->Reset());
		DX_CHECK(cmdList->Reset(cmdAllocator.Get(), nullptr));

		// Get a timestamp at the beginning and end of the command list.
		const UINT timestampHeapIndex = 2 * mCurrBackBuffer;
		cmdList->EndQuery(mQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampHeapIndex);

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

		cmdList->RSSetViewports(1, &mScreenViewport);
		cmdList->RSSetScissorRects(1, &mScissorRect);

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		FLOAT clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
		cmdList->ClearRenderTargetView(currentBackBufferView, clearColor, 0, nullptr);
		cmdList->ClearDepthStencilView(mDsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		cmdList->OMSetRenderTargets(1, &currentBackBufferView, true, &mDsvHeap->GetCPUDescriptorHandleForHeapStart());

		//BeginDraw
		RENDER_PASS("Mesh")
		{
			cmdList->SetPipelineState(mWireframeRendering ? mWireframeMeshPSO.Get() : mMeshPSO.Get());
			ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
			cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			cmdList->SetGraphicsRootSignature(mMeshRootSig.Get());
			auto passCB = mCurrFrameResource->PassCB->Resource();
			cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

			UINT objCBByteSize = CalcConstantBufferByteSize(sizeof(PerObjectCB));
			auto objectCB = mCurrFrameResource->ObjectCB->Resource();

			// For each render item...
			for (size_t i = 0; i < mOpaqueRitems.size(); ++i)
			{
				auto ri = mOpaqueRitems[i];
				RENDER_PASS(ri->Name.c_str())
				{
					CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
					tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

					D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

					cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
					cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
					cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
					cmdList->SetGraphicsRootDescriptorTable(0, tex);
					cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
					cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
				}
			}
		}

		//DrawImGui
		RENDER_PASS("Imgui")
		{
			mImguiDrawer->BeginDrawImgui(cmdList, mWindow->GetWidth(), mWindow->GetHeight());
			DrawMenuBar();
			if(showImguiDemo)
				ImGui::ShowDemoWindow(&showImguiDemo);
			DrawDebugInfo();
			mImguiDrawer->EndDrawImgui(cmdList);
		}

		//EndFrame
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

		//End Query
		cmdList->EndQuery(mQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampHeapIndex + 1);
		cmdList->ResolveQueryData(mQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampHeapIndex, 2, mQueryResult.Get(), timestampHeapIndex * sizeof(UINT64));

		mCurrentFence = mQueues->GetGraphicsQueue()->ExecuteCommandList(cmdList.Get());

		// swap the back and front buffers
		DX_CHECK(mSwapchain->Present(mVSync?1:0, 0));
		mCurrBackBuffer = (mCurrBackBuffer + 1) % BACKBUFFER_COUNT;
	}

	bool Application::EnumerateAdapters(D3D_FEATURE_LEVEL featureLevel)
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> candidateAdapter;
		for (uint32_t i = 0; mFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidateAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 adapterDesc;
			candidateAdapter->GetDesc1(&adapterDesc);
			if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
				SUCCEEDED(D3D12CreateDevice(candidateAdapter.Get(), featureLevel, __uuidof(ID3D12Device), nullptr)))
			{
				mAdapterDesc = adapterDesc;
				DX_CHECK(candidateAdapter.As(&mAdapter));
				return true;
			}
		}
		return false;
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
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_1;
		if (!EnumerateAdapters(featureLevel))
		{
			featureLevel = D3D_FEATURE_LEVEL_12_0;
			if (!EnumerateAdapters(featureLevel))
			{
				featureLevel = D3D_FEATURE_LEVEL_11_1;
				if (!EnumerateAdapters(featureLevel))
				{
					featureLevel = D3D_FEATURE_LEVEL_11_0;
					if (!EnumerateAdapters(featureLevel))
					{
						throw std::runtime_error("Unable to find a high performance GPU");
					}
				}
			}
		}
		
		// Create device
		if (!SUCCEEDED(D3D12CreateDevice(mAdapter.Get(), featureLevel, IID_PPV_ARGS(&mDevice))))
			throw std::runtime_error("Unable to create Device");

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

		bool allowTearing = true;
		ComPtr<IDXGIFactory4> factory4;
		if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
		{
			ComPtr<IDXGIFactory5> factory5;
			if (SUCCEEDED(factory4.As(&factory5)))
			{
				if (FAILED(factory5->CheckFeatureSupport(
					DXGI_FEATURE_PRESENT_ALLOW_TEARING,
					&allowTearing, sizeof(allowTearing))))
				{
					allowTearing = FALSE;
				}
			}
		}

		mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		mDsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		mCbvSrvUavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	void Application::InitCommandObjects()
	{
		mQueues = new Moon::CommandQueueManager(mDevice.Get());

		for (int i = 0; i < BACKBUFFER_COUNT; i++)
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

	void Application::InitQuery()
	{
		DX_CHECK(mQueues->GetGraphicsQueue()->GetCommandQueue()->GetTimestampFrequency(&mTimestampFrequency));

		// Two timestamps for each frame.
		const UINT resultCount = 2 * BACKBUFFER_COUNT;
		const UINT resultBufferSize = resultCount * sizeof(UINT64);
		D3D12_QUERY_HEAP_DESC timestampHeapDesc = {};
		timestampHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		timestampHeapDesc.Count = resultCount;

		DX_CHECK(mDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(resultBufferSize),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&mQueryResult)
		));
		DX_CHECK(mDevice->CreateQueryHeap(&timestampHeapDesc, IID_PPV_ARGS(&mQueryHeap)));
	}

	void Application::InitSwapchain()
	{
		mSwapchain.Reset();

		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(DXGI_SWAP_CHAIN_DESC1));
		sd.Width = mWindow->GetWidth();
		sd.Height = mWindow->GetHeight();
		sd.Format = mBackBufferFormat;
		sd.Scaling = DXGI_SCALING_NONE;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = BACKBUFFER_COUNT;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		DX_CHECK(mFactory->CreateSwapChainForHwnd(mQueues->GetGraphicsQueue()->GetCommandQueue(), mWindow->GetWindowHandle(), &sd, nullptr, nullptr, &mSwapchain));
		DX_CHECK(mFactory->MakeWindowAssociation(mWindow->GetWindowHandle(), DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER));
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

	void Application::Resize()
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
			mWindow->GetWidth(), mWindow->GetHeight(),
			mBackBufferFormat,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));

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
		depthStencilDesc.Width = mWindow->GetWidth();
		depthStencilDesc.Height = mWindow->GetHeight();
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
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
		mScreenViewport.Width = static_cast<float>(mWindow->GetWidth());
		mScreenViewport.Height = static_cast<float>(mWindow->GetHeight());
		mScreenViewport.MinDepth = 0.0f;
		mScreenViewport.MaxDepth = 1.0f;

		mScissorRect = { 0, 0, mWindow->GetWidth(), mWindow->GetHeight() };
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
		meshPsoDesc.SampleDesc.Count = 1;
		meshPsoDesc.SampleDesc.Quality = 0;
		meshPsoDesc.DSVFormat = mDepthStencilFormat;
		DX_CHECK(mDevice->CreateGraphicsPipelineState(&meshPsoDesc, IID_PPV_ARGS(&mMeshPSO)));

		meshPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		DX_CHECK(mDevice->CreateGraphicsPipelineState(&meshPsoDesc, IID_PPV_ARGS(&mWireframeMeshPSO)));
	}

	void Application::LoadImages()
	{
		auto lostEmpire = std::make_unique<Texture>();
		lostEmpire->Name = "lostEmpireTex";
		lostEmpire->Filename = L"../assets/lost-empire/lost_empire-RGBA.dds";
		DX_CHECK(DirectX::CreateDDSTextureFromFile12(mDevice.Get(),
			mCommandList[mCurrBackBuffer].Get(), lostEmpire->Filename.c_str(),
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
			mCommandList[mCurrBackBuffer].Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
		geo->IndexBufferGPU = CreateDefaultBuffer(mDevice.Get(),
			mCommandList[mCurrBackBuffer].Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

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
		leRitem->Name = "lostEmpire";
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

	void Application::GetQueryResult()
	{
		void* pData = nullptr;
		const D3D12_RANGE emptyRange = {};
		D3D12_RANGE readRange = {};
		readRange.Begin = (2 * mCurrBackBuffer) * sizeof(UINT64);
		readRange.End = readRange.Begin + 2 * sizeof(UINT64);

		DX_CHECK(mQueryResult->Map(0, &readRange, &pData));
		const UINT64* pTimestamps = reinterpret_cast<UINT64*>(static_cast<UINT8*>(pData) + readRange.Begin);
		const UINT64 timeStampDelta = pTimestamps[1] - pTimestamps[0];
		mQueryResult->Unmap(0, &emptyRange);

		mFrameStats.AddTimestamp(float((timeStampDelta * 1000)) / (float)(mTimestampFrequency));
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

	void Application::ToggleVSync()
	{
		mVSync = !mVSync; 
		std::cout << "VSync is " << (mVSync ? "On" : "Off") << std::endl;
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		Resize();
		return false;
	}

	void Application::DrawMenuBar()
	{
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				//if (ImGui::MenuItem("New")) {}
				//if (ImGui::MenuItem("Open", "Ctrl+O")) {}
				if (ImGui::MenuItem("Close", "ESC"))
				{
					PostQuitMessage(0);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View"))
			{
				ImGui::MenuItem("Show ImGui Demo", "", &showImguiDemo);
				ImGui::MenuItem("Wireframe View", "F1", &mWireframeRendering);
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}

	void Application::DrawDebugInfo()
	{
		if (ImGui::Begin("Debug Informations"))
		{
			ImGui::Text("Performance");
			ImGui::Text("CPU: %3.2f ms (avg %3.2f ms)", mTimer.DeltaTime()*1000, mTotalCpuTimeMS/mFrameNumber);
			ImGui::Text("GPU: %3.2f ms (avg %3.2f ms)", mFrameStats.GetCurrentGpuTime(), mFrameStats.GetAverageGpuTime());
			ImGui::Separator();
			ImGui::Text("Gpu Information");
			ImGui::Text("Name: %ls", mAdapterDesc.Description);
			ImGui::Text("VRAM: %d mb", mAdapterDesc.DedicatedVideoMemory /1024 /1024);
			ImGui::End();
		}
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowResizeEvent>(MN_BIND_EVENT_FN(Application::OnWindowResize));

		if (!e.mHandled) mImguiDrawer->OnEvent(e);
		if (!e.mHandled) mCamera->OnEvent(e);
	}
}