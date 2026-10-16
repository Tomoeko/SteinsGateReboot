#include "sgre/text/render_context.hpp"

#include "glyph_atlas_owner_fixture.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestActualEmptyDefaultsAndControlWords() {
	text_atlas_test::Backend backend;
	sgre::text::TextRenderContext context(backend, true);
	const auto& state = context.State();
	Require(!state.Vertical && state.TimeScale == 1 && state.FontScale == 1 && state.ShownElementCount == 0 && state.ClipMode == 0 && state.Size.X == 0 && state.Size.Y == 0, "actual constructed core/task controls differ");
	Require(context.Atlas().State().Filtering && !context.Atlas().State().Texture && backend.Observed.Creates == 0, "real empty text context must not allocate a font/atlas texture");
	context.SetVertical(true);
	context.SetTimeScale(std::bit_cast<float>(std::uint32_t{0x7fc01234}));
	context.SetFontScale(-3.5f);
	context.SetShownElementCount(0xffffffff);
	Require(state.Vertical && std::bit_cast<std::uint32_t>(state.TimeScale) == 0x7fc01234 && state.FontScale == -3.5f && state.ShownElementCount == 0xffffffff, "native scalar setters must preserve words without invented clamps");
}

static void TestClipRecomputeAndUpdateOrdering() {
	text_atlas_test::Backend backend;
	sgre::text::TextRenderContext context(backend);
	context.SetCoordinate({10, 20});
	Require(context.State().ClippingBounds.Left == 1 && context.State().ClippingBounds.Right == -1, "mode0 must compute the native inactive clip sentinel");
	context.SetClipMode(2);
	Require(context.State().ClippingBounds.Left == 1, "the native mode setter does not recompute clipping");
	context.SetClipOffset({3, -4});
	Require(context.State().ClippingBounds.Left == 13 && context.State().ClippingBounds.Top == 16 && context.State().ClippingBounds.Right == 13 && context.State().ClippingBounds.Bottom == 16, "fixed empty-view clipping must use coordinate plus clip offset");
	context.SetOffsetPosition({99, 100});
	context.SetShownElementCount(123);
	context.FinalizeEmpty();
	Require(context.State().ShownElementCount == 0 && context.State().OffsetPosition.X == 0 && context.State().ClipOffset.X == 0 && context.State().ScrollOffset.Y == 0 && context.State().ClippingBounds.Left == 13, "native update computes clip before clearing all three coordinate pairs");
	context.FinalizeEmpty();
	Require(context.State().ClippingBounds.Left == 10 && context.State().ClippingBounds.Top == 20, "the following update must observe the cleared clip offset");
	context.SetClipMode(-100);
	context.FinalizeEmpty();
	Require(context.State().ClippingBounds.Top == 1 && context.State().ClippingBounds.Bottom == -1, "unsupported numeric native clip modes retain the actual no-clip branch");
}

static void TestRequestedSizePrecedesClearAndActualViewFinalization() {
	text_atlas_test::Backend backend;
	sgre::text::TextRenderContext context(backend);
	context.SetCoordinate({11, 13});
	context.SetClipMode(2);
	context.SetClipOffset({5, 7});
	context.SetOffsetPosition({17, 19});
	context.SetShownElementCount(23);
	std::vector<int> order;
	sgre::text::TextFontResetCallbacks callbacks;
	callbacks.SelectNormal = [&] {
		Require(context.State().RenderSize.X == 320 && context.State().RenderSize.Y == 180 && context.State().Size.X == 0 && context.State().ShownElementCount == 23 && context.State().OffsetPosition.X == 17, "size request must be stored before clear's normal selection and before facade state retirement");
		order.push_back(1);
	};
	callbacks.MeasureNormal = [&](auto unit) {
		Require(unit == 0x3000, "resize clear measures the real selected font's U3000 code unit");
		order.push_back(2);
		return sgre::resource::GlyphSize{12, 24};
	};
	callbacks.SelectRuby = [&] {
		order.push_back(3);
	};
	context.SetRenderSize({320, 180}, callbacks);
	Require(order == std::vector<int>{1, 2, 3} && context.State().Size.X == 0 && context.State().ShownElementCount == 0 && context.State().OffsetPosition.X == 0 && context.State().ClippingBounds.Left == 1, "resize must clear through actual font/geometry phases without prematurely changing fixed-view size");
	context.FinalizeEmpty();
	Require(context.State().Size.X == 320 && context.State().Size.Y == 180 && context.State().ClippingBounds.Left == 16 && context.State().ClippingBounds.Top == 20 && context.State().ClippingBounds.Right == 336 && context.State().ClippingBounds.Bottom == 200, "done must install the requested extent before clipping, using retained coordinate and clip offset");
	context.SetShownElementCount(41);
	context.SetRenderSize({320, 180}, callbacks);
	Require(order.size() == 3 && context.State().ShownElementCount == 0 && context.State().ClippingBounds.Right == -1, "even equal render sizes must invoke actual clear while unchanged font keys avoid a new provider selection");
	context.SetRenderSize({-0.0f, -4}, callbacks);
	Require(context.State().Size.X == 320 && context.State().Size.Y == 180, "resize must retain prior fixed-view extent until true done");
	context.FinalizeEmpty();
	Require(std::bit_cast<std::uint32_t>(context.State().Size.X) == 0 && context.State().Size.Y == -4, "ordered zero falls back to real empty measured bounds while negative requested sizes remain native values");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc01234});
	context.SetRenderSize({nan, -0.0f}, callbacks);
	context.FinalizeEmpty();
	Require(std::bit_cast<std::uint32_t>(context.State().RenderSize.X) == 0x7fc01234 && std::bit_cast<std::uint32_t>(context.State().Size.X) == 0x7fc01234 && std::bit_cast<std::uint32_t>(context.State().Size.Y) == 0 && backend.Observed.Creates == 0, "native resize preserves unordered requests without clamping or allocating an invented texture");

	sgre::text::TextRenderContext partial(backend);
	partial.SetShownElementCount(7);
	sgre::text::TextFontResetCallbacks throwing;
	throwing.SelectNormal = [] {
		throw std::runtime_error("actual font callback failed");
	};
	throwing.MeasureNormal = [](auto) {
		return sgre::resource::GlyphSize{};
	};
	bool failed = false;
	try {
		partial.SetRenderSize({-3, 5}, throwing);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed && partial.State().RenderSize.X == -3 && partial.State().RenderSize.Y == 5 && partial.State().Size.X == 0 && partial.State().ShownElementCount == 7, "failed clear callbacks must preserve the prior native request writes and leave later facade clear unwritten");
}

int main() {
	try {
		TestActualEmptyDefaultsAndControlWords();
		TestClipRecomputeAndUpdateOrdering();
		TestRequestedSizePrecedesClearAndActualViewFinalization();
		std::cout << "Actual empty TextRender context controls and native clipping/update ordering passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
