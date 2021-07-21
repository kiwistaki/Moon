#pragma once
#include "dx_utils.h"
#include "Camera.h"
#include "CommandQueue.h"
#include "Texture.h"
#include "Material.h"
#include "Mesh.h"
#include "Math.h"
#include "RenderDoc.h"
#include "Timer.h"
#include "Event.h"
#include "ImguiDrawer.h"

namespace Moon
{
	struct PerPassCB
	{
		DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	};

	struct PerObjectCB
	{
		DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
	};

	struct FrameResource
	{
	public:
		FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount)
		{
			PassCB = std::make_unique<UploadBuffer<PerPassCB>>(device, passCount, true);
			ObjectCB = std::make_unique<UploadBuffer<PerObjectCB>>(device, objectCount, true);
		}

		FrameResource(const FrameResource& rhs) = delete;
		FrameResource& operator=(const FrameResource& rhs) = delete;
		~FrameResource() {}

		std::unique_ptr<UploadBuffer<PerObjectCB>> ObjectCB = nullptr;
		std::unique_ptr<UploadBuffer<PerPassCB>> PassCB = nullptr;
	};

	struct RenderItem
	{
		RenderItem() = default;

		DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

		int NumFramesDirty = BACKBUFFER_COUNT;
		D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		UINT ObjCBIndex = -1;
		Material* Mat = nullptr;
		MeshGeometry* Geo = nullptr;
		UINT IndexCount = 0;
		UINT StartIndexLocation = 0;
		int BaseVertexLocation = 0;
	};

	struct FrameStats
	{
		float* gpuTimes;
		int timestampIndex = 0;

		static const int numberOfTimestamps = 100;

		void Init()
		{	
			gpuTimes = new float[numberOfTimestamps];
			for (size_t i = 0; i < numberOfTimestamps; i++)
			{
				gpuTimes[i] = 0.0f;
			}
		}

		void Deinit()
		{
			delete gpuTimes;
		}

		void AddTimestamp(float ts)
		{
			gpuTimes[timestampIndex++] = ts;
			timestampIndex = timestampIndex % numberOfTimestamps;
		}

		float GetCurrentGpuTime()
		{
			if (timestampIndex == 0)
				return gpuTimes[numberOfTimestamps];
			else
				return gpuTimes[timestampIndex - 1];
		}

		float GetAverageGpuTime()
		{
			float avg = 0.0f;
			for (size_t i = 0; i < numberOfTimestamps; i++)
			{
				avg += gpuTimes[i];
			}
			return avg /= numberOfTimestamps;
		}

	};

	class Application
	{
	public:
		void Init();
		void Cleanup();
		void Draw();
		void Run();

		void OnEvent(Event& e);
		static LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	private:
		void DrawMesh();
		void InitMainWindow();
		void InitD3D12();
		void InitCommandObjects();
		void InitQuery();
		void InitSwapchain();
		void InitDescriptorHeaps();
		void Resize();

		void InitPipeline();
		void LoadImages();
		void LoadMeshes();
		void InitScene();


		void GetQueryResult();

		std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
		Microsoft::WRL::ComPtr<ID3DBlob> LoadShaderBinary(const std::wstring& filename);
		Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
			ID3D12Device* device,
			ID3D12GraphicsCommandList* cmdList,
			const void* initData,
			UINT64 byteSize,
			Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

		void SetFullscreen(bool fullscreen);
		void ToggleWireframeRendering() { mWireframeRendering = !mWireframeRendering; }
		void OnMouseDown(WPARAM btnState, int x, int y);
		void OnMouseUp(WPARAM btnState, int x, int y);
		void OnMouseMove(WPARAM btnState, int x, int y);
		void OnMouseScroll(short x, short y);
		void OnKeyUp(const WPARAM key);
		void OnKeyDown(const WPARAM key, const uint16_t repeatCount);
		void OnResize();
		bool OnWindowResize(WindowResizeEvent& e);

		void DrawMenuBar();
		void DrawDebugInfo();

	private:
		CommandQueueManager* mQueues = nullptr;
		RenderDoc* mRenderDoc = nullptr;
		Timer mTimer;
		bool isD3D12Initialized = false;
		ImguiDrawer* mImguiDrawer = nullptr;
		bool showImguiDemo = false;

		//Win32 stuff
		HINSTANCE		mhAppInst = nullptr; // application instance handle
		HWND			mhMainWnd = nullptr; // main window handle
		RECT			mRect;
		bool			mRunning = true;
		bool			mMinimized = false;
		bool			mAppPaused = false;  // is the application paused?
		bool			mMaximized = false;  // is the application maximized?
		bool			mResizing = false;   // are the resize bars being dragged?
		bool			mFullscreenState = false;// fullscreen enabled
		int				mClientWidth = 1600;
		int				mPreviousClientWidth = 1600;
		int				mClientHeight = 900;
		int				mPreviousClientHeight = 900;
		bool			m4xMsaaState = false;
		std::wstring	mMainWndCaption = L"Moon";
		bool			mWireframeRendering = false;

		//D3D12 related stuff
		Microsoft::WRL::ComPtr<IDXGIFactory6> mFactory;
		Microsoft::WRL::ComPtr<IDXGIAdapter4> mAdapter;
		DXGI_ADAPTER_DESC1 mAdapterDesc;
		Microsoft::WRL::ComPtr<ID3D12Device8> mDevice;

		int mCurrBackBuffer = 0;
		Microsoft::WRL::ComPtr<IDXGISwapChain1> mSwapchain;
		Microsoft::WRL::ComPtr<ID3D12Resource> mSwapchainBuffer[BACKBUFFER_COUNT];
		Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;
		DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		UINT m4xMsaaQuality = 0;
		D3D12_VIEWPORT mScreenViewport;
		D3D12_RECT mScissorRect;
		
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
		UINT64 mCurrentFence = 0;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;

		Microsoft::WRL::ComPtr<ID3D12QueryHeap> mQueryHeap;
		Microsoft::WRL::ComPtr<ID3D12Resource> mQueryResult;
		UINT64 mTimestampFrequency;
		int mFrameNumber = 0;
		float mTotalCpuTimeMS = 0.0f;
		FrameStats mFrameStats;

		UINT mRtvDescriptorSize = 0;
		UINT mDsvDescriptorSize = 0;
		UINT mCbvSrvUavDescriptorSize = 0;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mMeshRootSig;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> mMeshPSO;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> mWireframeMeshPSO;

		Camera* mCamera = nullptr;
		std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
		std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
		std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
		std::vector<std::unique_ptr<RenderItem>> mAllRitems;
		std::vector<RenderItem*> mOpaqueRitems;

		std::vector<std::unique_ptr<FrameResource>> mFrameResources;
		FrameResource* mCurrFrameResource = nullptr;
		int mCurrFrameResourceIndex = 0;
	};
}
