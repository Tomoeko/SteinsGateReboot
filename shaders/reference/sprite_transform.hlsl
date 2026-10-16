// Recovered VS1: SHA-256 f24652f5fa99c189f7824454f4b0b3190764b444ba7a5cb5c9463d0e2c7e2334.
// Each raw constant register supplies x/y/translation to one output component.
column_major float4x4 modelViewProj : register(c0);

struct SpriteVertexInput {
	float2 Position : POSITION;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

struct SpriteVertexOutput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

SpriteVertexOutput main(SpriteVertexInput input) {
	const float3 position = float3(input.Position, 1.0f);
	const float4x4 registers = transpose(modelViewProj);
	SpriteVertexOutput output;
	output.Position.x = dot(position, registers[0].xyw);
	output.Position.y = dot(position, registers[1].xyw);
	output.Position.z = dot(position, registers[2].xyw);
	output.Position.w = dot(position, registers[3].xyw);
	output.Color = input.Color;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}
