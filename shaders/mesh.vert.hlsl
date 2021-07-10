struct VertexIn {
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float3 Color : COLOR;
	float2 TexCoord: TEXCOORD;
};

struct VertexOut {
	float4 PosH : SV_POSITION;
	float3 PosW : POSITION;
	float3 NormalW : NORMAL;
	float3 Color : COLOR0;
	float2 TexCoord : TEXCOORD;
};

cbuffer cameraBuffer : register(b0) {
	float4x4 view;
	float4x4 proj;
	float4x4 viewproj;
};

struct ObjectData {
	float4x4 model;
};
StructuredBuffer<ObjectData> objectBuffer : register(t0, space1);

VertexOut VS(VertexIn vin, uint vertexID: SV_VERTEXID)
{
	VertexOut vout = (VertexOut)0;
	float4x4 modelMatrix = objectBuffer[vertexID].model;

	float4 posW = mul(float4(vin.Position, 1.0f), modelMatrix);
	vout.PosW = posW.xyz;
	vout.NormalW = mul(vin.Normal, (float3x3)modelMatrix);
	vout.PosH = mul(posW, viewproj);
	//float4 texC = mul(float4(vin.TexCoord, 0.0f, 1.0f), gTexTransform);
	vout.TexCoord = vin.TexCoord;//mul(texC, matData.MatTransform).xy;
	vout.Color = vin.Color;
	return vout;
}
