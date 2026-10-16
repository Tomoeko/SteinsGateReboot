// Recovered PS300: SHA-256 588529ae6c3a56c78c4d39508be08982791b18d58e21e649ef0647f4b97bdec1.
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct SpritePixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(SpritePixelInput input) : SV_Target {
	const float4 sampled_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float4 modulated_color = input.Color * sampled_color;

	return modulated_color * float4(2.0f, 2.0f, 2.0f, 1.0f);
}
