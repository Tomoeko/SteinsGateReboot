// Recovered PS600: SHA-256 3fdaef28e2588510167ffe55ac1bcaafc56e18b72c50032af4c61da5be2d2ceb.
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

	return input.Color * color;
}
