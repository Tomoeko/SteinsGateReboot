// Recovered PS180: SHA-256 432c0475136871f87024a041af9eff6c04103ecbf626f0ebb1a20c24107238c9.
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct SpritePixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(SpritePixelInput input) : SV_Target {
	return colorTexture.Sample(colorSampler, input.TextureCoordinates);
}
