// Recovered PS1205: SHA-256 25b33897c9c9dd667f06eae6ad8b59e1f8cbcd902345bf443d87906167395450.
float4 screenMulOffset;
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> cDestTexTexture : register(t2);
SamplerState colorSampler : register(s0);
SamplerState cDestTexSampler : register(s2);

struct CopiedPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(CopiedPixelInput input) : SV_Target {
	const float4 source_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float2 destination_coordinates = input.Position.xy * screenMulOffset.xy + screenMulOffset.zw;
	const float4 destination_color = cDestTexTexture.Sample(cDestTexSampler, destination_coordinates);

	float3 combined_color = source_color.rgb * input.Color.rgb + destination_color.rgb;
	const float opacity = source_color.a * input.Color.a;
	combined_color = min(combined_color, 1.0f);
	const float3 color_difference = combined_color - destination_color.rgb;
	const float3 result_color = opacity * color_difference + destination_color.rgb;

	return float4(result_color, opacity);
}
