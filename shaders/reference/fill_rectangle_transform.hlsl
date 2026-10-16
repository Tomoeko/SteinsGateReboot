// Recovered VS3: SHA-256 b01deddf790f49d10796c8ca49cbd1262f71980995a6c2226e9a49fa238b8fcf.
// Each raw constant register supplies x/y/translation to one output component.
column_major float4x4 modelViewProj : register(c0);
float4 constVtxColor : register(c4);

struct FillRectangleVertexInput {
	float2 Position : POSITION;
};

struct FillRectangleVertexOutput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

FillRectangleVertexOutput main(FillRectangleVertexInput input) {
	const float3 position = float3(input.Position, 1.0f);
	const float4x4 registers = transpose(modelViewProj);
	FillRectangleVertexOutput output;
	output.Position.x = dot(position, registers[0].xyw);
	output.Position.y = dot(position, registers[1].xyw);
	output.Position.z = dot(position, registers[2].xyw);
	output.Position.w = dot(position, registers[3].xyw);
	output.Color = constVtxColor;
	output.TextureCoordinates = float2(0.0f, 0.0f);

	return output;
}
