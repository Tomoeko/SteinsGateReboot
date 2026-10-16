// Native masked copied runtime PS1150; vertex color is intentionally absent.
float2 screenMul;
float4 screenMulOffset;
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> alphaTexture : register(t1);
Texture2D<float4> cDestTexTexture : register(t2);
SamplerState colorSampler : register(s0);
SamplerState alphaSampler : register(s1);
SamplerState cDestTexSampler : register(s2);

struct MaskPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(MaskPixelInput input) : SV_Target {
	const float4 source_color = colorTexture.Sample(colorSampler, input.TextureCoordinates);
	const float2 destination_coordinates = input.Position.xy * screenMulOffset.xy + screenMulOffset.zw;
	const float4 destination_color = cDestTexTexture.Sample(cDestTexSampler, destination_coordinates);
	const float3 inverse_destination = 1.0f - destination_color.rgb;
	const float3 inverse_source = 1.0f - source_color.rgb;
	const float3 upper = 1.0f - (inverse_destination * inverse_source) * 2.0f;
	const float3 product = destination_color.rgb * source_color.rgb;
	const float3 lower = product + product;
	const float3 combined_color = destination_color.rgb < 0.5f ? lower : upper;
	const float3 difference = combined_color - destination_color.rgb;
	const float2 mask_coordinates = input.Position.xy * screenMul;
	const float mask_alpha = alphaTexture.Sample(alphaSampler, mask_coordinates).a;
	const float opacity = source_color.a * mask_alpha;
	const float3 result = opacity * difference + destination_color.rgb;

	return float4(result, opacity);
}
