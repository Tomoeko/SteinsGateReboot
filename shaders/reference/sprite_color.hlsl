// Recovered PS240: SHA-256 bbe091625a725979432e33a711fd4d2ed3d14e1a1d2e58c76fa278993d6d8556.
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct SpritePixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(SpritePixelInput input) : SV_Target {
	const float4 sampled_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);

	return input.Color * sampled_color;
}
