#pragma once 
#include "dx_utils.h"
#include "Math.h"

struct Material
{
	std::string Name;

	int MatCBIndex = -1;

	int DiffuseSrvHeapIndex = -1;
	int NormalSrvHeapIndex = -1;

	int NumFramesDirty = BACKBUFFER_COUNT;

	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};