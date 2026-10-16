// Recovered runtime PS1137; native operation9, see docs/recovery/copied_darken_shader.md.
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
	const float3 combined_color = min(destination_color.rgb, source_color.rgb);
	const float3 difference = combined_color - destination_color.rgb;
	const float opacity = source_color.a;
	const float3 result = opacity * difference + destination_color.rgb;

	return float4(result, opacity);
}
