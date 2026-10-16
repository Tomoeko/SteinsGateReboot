// Native runtime PS2060; phases 3/5 share source alpha, phase 4 multiplies the preceding mask.
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct MaskPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(MaskPixelInput input) : SV_Target {
	const float4 sampled = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float4 source = float4(sampled.rrr, 1.0f);
	const float alpha = source.a;

	return float4(source.rgb, alpha);
}
