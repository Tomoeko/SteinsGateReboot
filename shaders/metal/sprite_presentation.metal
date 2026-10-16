#include <metal_stdlib>

struct SpritePresentationVertex {
	metal::float4 Position [[position]];
};

vertex SpritePresentationVertex SpritePresentationVertexMain(uint index [[vertex_id]]) {
	const metal::float2 positions[3] = {metal::float2(-1, -1), metal::float2(3, -1), metal::float2(-1, 3)};
	SpritePresentationVertex result;
	result.Position = metal::float4(positions[index], 0, 1);

	return result;
}

fragment metal::float4 SpritePresentationFragmentMain(SpritePresentationVertex input [[stage_in]], metal::texture2d<float, metal::access::read> source [[texture(0)]]) {
	// Matching physical extents select the same integer texel, with no sampler or rescaling.
	return source.read(metal::uint2(input.Position.xy));
}

fragment metal::float4 SpritePresentationScaledFragmentMain(SpritePresentationVertex input [[stage_in]], metal::texture2d<float> source [[texture(0)]], constant metal::float4& content [[buffer(0)]]) {
	// This platform resize filter does not change scene coordinates or the logical render target.
	constexpr metal::sampler sampling(metal::coord::normalized, metal::address::clamp_to_edge, metal::filter::linear);
	const auto uv = (input.Position.xy - content.xy) * content.zw;
	if (metal::any(uv < metal::float2(0)) || metal::any(uv >= metal::float2(1))) {
		metal::discard_fragment();
	}

	return source.sample(sampling, uv);
}
