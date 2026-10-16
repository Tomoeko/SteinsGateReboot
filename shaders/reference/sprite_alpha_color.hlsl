// Recovered PS420: SHA-256 2256f870a01f74354acf42c03718e47a8d896b34165c299726cc6f43ecfd3fd2.
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

	return input.Color * color;
}
