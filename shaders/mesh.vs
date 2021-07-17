struct VertexIn {
	float3 PosL : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC: TEXCOORD;
};

struct VertexOut {
	float4 PosH : SV_POSITION;
	float3 PosW : POSITION;
	float3 NormalW : NORMAL;
	float2 TexC : TEXCOORD;
};

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

cbuffer cbPerPass : register(b1) {
	float4x4 gView;
	float4x4 gProj;
	float4x4 gViewProj;
};

VertexOut VS(VertexIn vin, uint vertexID: SV_VERTEXID)
{
	VertexOut vout = (VertexOut)0.0f;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    vout.PosH = mul(posW, gViewProj);
    //float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = vin.TexC;//mul(texC, gMatTransform).xy;
    return vout;
}
