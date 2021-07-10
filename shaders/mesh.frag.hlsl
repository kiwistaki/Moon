struct VertexOut {
	float4 PosH : SV_POSITION;
	float3 PosW : POSITION;
	float3 NormalW : NORMAL;
	float3 Color : COLOR0;
	float2 TexCoord : TEXCOORD;
};

Texture2D gDiffuseMap : register(t1);
SamplerState gsamPointWrap : register(s0);

float4 PS(VertexOut pin) : SV_TARGET
{
	return gDiffuseMap.Sample(gsamPointWrap, pin.TexCoord);
}