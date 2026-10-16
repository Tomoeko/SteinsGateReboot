// Native runtime PS2042; phases 3/5 share source alpha, phase 4 multiplies the preceding mask.

struct MaskPixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(MaskPixelInput input) : SV_Target {
	const float4 source = input.Color;
	const float alpha = source.a;

	return float4(source.rgb, alpha);
}
