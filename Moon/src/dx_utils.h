#pragma once
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#include "d3dx12.h"

#define USE_PIX
#include <pix.h>

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>

#include <comdef.h>

#define BACKBUFFER_COUNT 2

struct ScopedPerfMarker
{
	operator bool() { return true; }

	ScopedPerfMarker(ID3D12GraphicsCommandList* commandList, const char* name)
		: mCommandList(commandList)
	{
		PIXBeginEvent(commandList, 0, name);
	}

	~ScopedPerfMarker()
	{
		PIXEndEvent(mCommandList);
	}

	ID3D12GraphicsCommandList* mCommandList;
};

#define RENDER_PASS(name) \
	if (auto scopedPerfMarker = ScopedPerfMarker(mCommandList.Get(), name))

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

#ifdef DEBUG
#	ifndef DX_CHECK
#		define DX_CHECK(x)\
		{\
			HRESULT res = FAILED(x);\
			if(res)\
			{\
				std::wstring wfn = AnsiToWString(__FILE__);\
				throw DxException(res, L#x, wfn, __LINE__);\
			}\
		}
#	endif
#else
#	define DX_CHECK(x) (x)
#endif


#ifdef DEBUG
#	define DX12_ENABLE_DEBUG_LAYER (1)
#endif
#ifdef DX12_ENABLE_DEBUG_LAYER
#	include <dxgidebug.h>
#endif

static UINT CalcConstantBufferByteSize(UINT byteSize)
{
	return (byteSize + 255) & ~255;
}

//Forward declaration
template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);

		if (isConstantBuffer)
			mElementByteSize = CalcConstantBufferByteSize(sizeof(T));

		DX_CHECK(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploadBuffer)));

		DX_CHECK(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
	}

	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
	~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
			mUploadBuffer->Unmap(0, nullptr);

		mMappedData = nullptr;
	}

	ID3D12Resource* Resource()const
	{
		return mUploadBuffer.Get();
	}

	void CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
	BYTE* mMappedData = nullptr;

	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};


class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
		: ErrorCode(hr), FunctionName(functionName), Filename(filename), LineNumber(lineNumber)
	{
	}

	std::wstring ToString()const
	{
		_com_error err(ErrorCode);
		std::wstring msg = err.ErrorMessage();

		return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
	}

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
};
