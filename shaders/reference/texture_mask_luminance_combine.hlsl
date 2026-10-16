// Native runtime PS2061; phases 3/5 share source alpha, phase 4 multiplies the preceding mask.
float4 alphaMulAdd;
float2 screenMul;
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> alphaTexture : register(t1);
SamplerState colorSampler : register(s0);
SamplerState alphaSampler : register(s1);

struct MaskPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(MaskPixelInput input) : SV_Target {
	const float2 mask_coordinates = input.Position.xy * screenMul;
	const float mask_alpha = alphaTexture.Sample(alphaSampler, mask_coordinates).a;
	const float4 sampled = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float4 source = float4(sampled.rrr, 1.0f);
	const float alpha = source.a;

	return float4(source.rgb, mask_alpha * alpha);
}
