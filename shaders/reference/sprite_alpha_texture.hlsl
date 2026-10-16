// Recovered PS360: SHA-256 eb4bc0fb770a1fb8e134000f0a83a8c5244ae48b0fab4783ec284671b4796bce.
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct SpritePixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(SpritePixelInput input) : SV_Target {
	const float4 sampled_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float4 color = float4(1.0f, 1.0f, 1.0f, sampled_color.a);

	return color;
}
