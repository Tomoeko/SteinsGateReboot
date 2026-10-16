// Recovered PS540: SHA-256 865d0c970231174b4867a20f11b5d06f3c9c197c05312167d0370cf276930e74.
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct SpritePixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(SpritePixelInput input) : SV_Target {
	const float4 sampled_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float4 color = float4(sampled_color.rrr, 1.0f);

	return color;
}
