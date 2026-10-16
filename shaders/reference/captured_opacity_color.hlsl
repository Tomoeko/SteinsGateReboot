// Recovered PS2040: SHA-256 c9252e779fc8add6eeee2c57c7396e49d2ec65ce7e9d94c79a1482be2176ac1d.
float phase;
float invVague;
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);
Texture2D<float4> alphaTexture : register(t1);
SamplerState alphaSampler : register(s1);

struct CapturedOpacityPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
	float2 OpacityCoordinates : TEXCOORD1;
};

float4 main(CapturedOpacityPixelInput input) : SV_Target {
	float alpha = alphaTexture.Sample(alphaSampler, input.OpacityCoordinates).r;
	alpha = saturate((alpha - phase) * invVague + 1.0f);
	alpha = alpha * input.Color.a;
	float4 color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	color.a = alpha * color.a;

	return color;
}
