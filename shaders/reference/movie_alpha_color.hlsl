Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct MovieAlphaPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 ColorCoordinates : TEXCOORD0;
	float2 AlphaCoordinates : TEXCOORD1;
};

float4 main(MovieAlphaPixelInput input) : SV_Target {
	float4 color = colorTexture.Sample(colorSampler, input.ColorCoordinates);
	color.rgb = color.rgb * input.Color.rgb;
	float alpha = colorTexture.Sample(colorSampler, input.AlphaCoordinates).r;
	color.a = alpha * input.Color.a;

	return color;
}
