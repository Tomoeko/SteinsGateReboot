// Native runtime PS2069; phases 3/5 share source alpha, phase 4 multiplies the preceding mask.
float4 alphaMulAdd;
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct MaskPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(MaskPixelInput input) : SV_Target {
	const float4 sampled = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float4 source = float4(sampled.rrr, sampled.g);
	const float alpha = saturate(source.a * alphaMulAdd.x + alphaMulAdd.y);

	return float4(source.rgb, alpha);
}
