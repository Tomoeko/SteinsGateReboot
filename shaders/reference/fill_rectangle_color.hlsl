// Recovered PS60: SHA-256 93c2d188c6e57a05335b659e519c329defafe733c30dd775e70f91a3dec4ecca.
// The original single multiply squares every channel, including alpha.
struct FillRectanglePixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(FillRectanglePixelInput input) : SV_Target {
	return input.Color * input.Color;
}
