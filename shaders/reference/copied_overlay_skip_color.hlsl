// Recovered runtime PS1131, original occurrence1765, SHA-256 28c17ad4b3b1da5da4019644eb8f23e1aaa34a5506b57af614fde70ae523383a.
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
	const float2 destination_coordinates = input.Position.xy * screenMulOffset.xy + screenMulOffset.zw;
	const float4 destination_color = cDestTexTexture.Sample(cDestTexSampler, destination_coordinates);
	const float4 source_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float3 inverse_destination = 1.0f - destination_color.rgb;
	const float3 inverse_source = 1.0f - source_color.rgb;
	const float3 upper = 1.0f - (inverse_destination * inverse_source) * 2.0f;
	const float3 product = destination_color.rgb * source_color.rgb;
	const float3 lower = product + product;
	const float3 combined_color = destination_color.rgb < 0.5f ? lower : upper;
	const float3 difference = combined_color - destination_color.rgb;
	const float3 result = source_color.a * difference + destination_color.rgb;

	return float4(result, source_color.a);
}
