#include "sgre/scene/graphics_model_stack.hpp"
#include "sgre/scene/layer_camera_draw.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected camera scope failure: " + std::string(message));
}

static sgre::scene::LayerCameraFrameState Frame() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {1280, 720};
	state.View.Coefficients = {2.0f / 1280, 0, 0, -1, 0, -2.0f / 720, 0, 1, 0, 0, 1, 0};
	state.RawScissor = {0, 0, 1280, 720};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static void TestRealTaskCameraCompositionAndNesting() {
	sgre::scene::LayerCameraDrawScope frame(Frame(), {}, {}, 2);
	sgre::scene::ParallelTaskSet outer(1);
	sgre::scene::ParallelTaskSet inner(1);
	std::uint32_t draws = 0;
	sgre::scene::LayerCameraParameters camera;
	camera.Offset = {7, -2};
	camera.Scale = {2, 3};
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Draw = [&](auto, auto argument) {
		Require(argument == 0x87654321u && frame.Depth() == 1 && frame.CurrentModel().Coefficients[3] == -7 && frame.CurrentModel().Coefficients[7] == 2, "genuine child observes native negative offset and unchanged full draw argument");
		const auto saved = frame.CurrentModel();
		frame.Draw({}, inner, argument);
		Require(frame.Depth() == 1 && frame.CurrentModel().Coefficients == saved.Coefficients, "nested camera restores outer model owner and depth");
		++draws;
	};
	static_cast<void>(outer.Append(std::move(callback)));
	callback.Draw = [&](auto, auto) {
		Require(frame.Depth() == 2 && frame.CurrentModel().Coefficients[0] == 2 && frame.CurrentModel().Coefficients[5] == 3, "nested identity camera must retain real composed parent scale");
		RequireFailure([&] {
			frame.Draw({}, inner, 0);
		},
		    "capacity");
		++draws;
	};
	static_cast<void>(inner.Append(std::move(callback)));
	frame.Draw(camera, outer, 0x87654321u);
	Require(draws == 2 && frame.Depth() == 0, "empty initial model stack must regain its actual empty state");
	RequireFailure([&] {
		static_cast<void>(frame.CurrentModel());
	},
	    "no current");
}

static void TestIntersectionFallbackAndFailureRestoration() {
	std::vector<sgre::scene::MotionIntegerScissor> submissions;
	sgre::scene::LayerCameraDrawScope frame(Frame(), {}, [&](const auto& rectangle) {
		submissions.push_back(rectangle);
	});
	sgre::scene::ParallelTaskSet children(1);
	std::uint32_t draws = 0;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Draw = [&](auto, auto) {
		++draws;
	};
	static_cast<void>(children.Append(std::move(callbacks)));
	sgre::scene::LayerCameraParameters camera;
	camera.Clip = {2000, 2000, 2010, 2010};
	frame.Draw(camera, children, 0);
	Require(draws == 1 && submissions.empty(), "native empty intersection still draws genuine children with previous raw scissor");
	camera.Clip = {10.25f, 20.75f, 30.5f, 40.25f};
	frame.Draw(camera, children, 0);
	Require(draws == 2 && submissions.size() == 2 && frame.State().RawScissor.MinimumX == 0, "valid intersection must apply then restore original raw scissor");
	children.Clear();
	callbacks.Draw = [&](auto, auto) {
		throw std::runtime_error("prepared child failure");
	};
	static_cast<void>(children.Append(std::move(callbacks)));
	RequireFailure([&] {
		frame.Draw(camera, children, 0);
	},
	    "prepared child failure");
	Require(frame.Depth() == 0 && frame.State().RawScissor.MaximumX == 1280 && submissions.size() == 4, "authored callback failure must restore scissor/model before reporting failure");

	sgre::scene::LayerCameraDrawScope unavailable(Frame(), {}, {});
	RequireFailure([&] {
		unavailable.Draw(camera, children, 0);
	},
	    "renderer submission");
	Require(unavailable.Depth() == 0 && unavailable.State().RawScissor.MaximumX == 1280, "missing physical service must leave no stale frame scope");
}

static void TestMeasuredSafetyAndScaleStateConversion() {
	sgre::scene::LayerScaleCameraState source;
	source.Offset = {5, 7};
	source.Scale = {2, 4};
	source.RotationRadians = 0.5f;
	const auto camera = sgre::scene::PrepareLayerCameraParameters(source);
	Require(camera.Pivot.X == 0 && camera.Clip.MaximumX < camera.Clip.MinimumX && camera.Offset.X == 5 && camera.RotationRadians == 0.5f, "ScaleObject conversion preserves actual zero-pivot/disabled-clip constructor policy");
	auto invalid = camera;
	invalid.Scale.X = std::bit_cast<float>(0x7fc00001u);
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareLayerCameraMatrix({}, invalid));
	},
	    "finite pivot");
	invalid = camera;
	invalid.RotationRadians = 500000;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareLayerCameraMatrix({}, invalid));
	},
	    "reduction parity");
}

static void TestScissorCacheAndSubmissionFailures() {
	const sgre::scene::MotionBounds clip{10.25f, 20.75f, 30.5f, 40.25f};
	sgre::scene::LayerCameraParameters camera;
	camera.Clip = clip;
	auto state = Frame();
	const auto transformed = sgre::scene::TransformMotionScissorBounds(clip, state.View, {}, state.Viewport);
	state.AppliedRoundedScissor = sgre::scene::PrepareMotionScissorUpdate(transformed, {}).Rounded;
	std::size_t submissions = 0;
	std::size_t draws = 0;
	sgre::scene::LayerCameraDrawScope cached(state, {}, [&](const auto&) {
		++submissions;
	});
	sgre::scene::ParallelTaskSet children(1);
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Draw = [&](auto, auto) {
		Require(submissions == 0 && cached.State().RawScissor.MinimumX == transformed.MinimumX, "rounded cache suppression must still install current fractional raw scissor");
		++draws;
	};
	static_cast<void>(children.Append(std::move(callback)));
	cached.Draw(camera, children, 0);
	Require(draws == 1 && submissions == 1 && cached.State().RawScissor.MaximumX == 1280, "suppressed apply must still restore and physically submit the prior scissor");

	children.Clear();
	callback.Draw = [&](auto, auto) {
		++draws;
	};
	static_cast<void>(children.Append(std::move(callback)));
	sgre::scene::LayerCameraDrawScope failed(Frame(), {}, [&](const auto&) {
		throw std::runtime_error("physical apply rejected");
	});
	RequireFailure([&] {
		failed.Draw(camera, children, 0);
	},
	    "physical apply rejected");
	Require(draws == 1 && failed.Depth() == 0 && failed.State().RawScissor.MaximumX == 1280 && failed.State().AppliedRoundedScissor.MaximumX == 1280, "failed apply must skip child and preserve applied cache while restoring logical state");

	std::size_t attempts = 0;
	sgre::scene::LayerCameraDrawScope restore_failure(Frame(), {}, [&](const auto&) {
		if (++attempts == 2) {
			throw std::runtime_error("physical restore rejected");
		}
	});
	RequireFailure([&] {
		restore_failure.Draw(camera, children, 0);
	},
	    "physical restore rejected");
	Require(draws == 2 && restore_failure.Depth() == 0 && restore_failure.State().RawScissor.MaximumX == 1280 && restore_failure.State().AppliedRoundedScissor.MaximumX != 1280, "restore failure must retain truthful physical cache and unwind model");
	restore_failure.Draw(camera, children, 0);
	Require(draws == 3 && attempts == 3 && restore_failure.State().AppliedRoundedScissor.MaximumX == 1280, "later restoration must retry the actual stale physical cache");

	camera.Clip = {std::bit_cast<float>(0x7fc00001u), 0, 20, 20};
	failed.Draw(camera, children, 0);
	Require(draws == 4 && failed.Depth() == 0, "unordered native clip guard disables clipping without skipping actual children");
}

static void TestActualScaleViewCameraBoundary() {
	sgre::scene::LayerCameraDrawScope frame(Frame(), {}, {});
	sgre::scene::MotionLayerTasks parent(2);
	std::size_t draws = 0;
	std::size_t releases = 0;
	sgre::scene::LayerScaleTaskCallbacks callbacks;
	callbacks.Draw = [&](std::uint32_t argument) {
		Require(argument == 91 && frame.Depth() == 1 && frame.CurrentModel().Coefficients[0] == 2 && frame.CurrentModel().Coefficients[5] == 3 && frame.CurrentModel().Coefficients[3] == -7, "actual transferred ScaleView leaf must observe its genuine composed camera frame");
		++draws;
	};
	callbacks.Release = [&] {
		++releases;
	};
	callbacks.DrawWithCamera = [&](const auto& camera, auto& children, std::uint32_t argument) {
		frame.Draw(sgre::scene::PrepareLayerCameraParameters(camera), children, argument);
	};
	sgre::scene::LayerScaleView view(parent, std::move(callbacks));
	view.SetScale({2, 3});
	view.SetOffset({7, 0});
	parent.Group(sgre::scene::MotionTaskGroup::First).Draw(91);
	Require(draws == 0 && frame.Depth() == 0, "camera creation must preserve the actual leaf's paused state");
	view.SetVisible(true);
	parent.Group(sgre::scene::MotionTaskGroup::First).Draw(91);
	Require(draws == 1 && frame.Depth() == 0, "real First camera-to-child traversal must restore its frame after drawing");
	view.Retire();
	parent.Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(releases == 1 && frame.Depth() == 0, "camera task retirement must release the transferred leaf exactly once");
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(static_cast<char*>(static_cast<void*>(bytes.data())), bytes.size());
	if (!input) {
		throw std::runtime_error("camera native projection is truncated");
	}

	return bytes[0] | (std::uint32_t{bytes[1]} << 8) | (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

static float ReadFloat(std::istream& input) {
	return std::bit_cast<float>(ReadWord(input));
}

static sgre::scene::MotionBounds ReadBounds(std::istream& input) {
	sgre::scene::MotionBounds bounds;
	bounds.MinimumX = ReadFloat(input);
	bounds.MinimumY = ReadFloat(input);
	bounds.MaximumX = ReadFloat(input);
	bounds.MaximumY = ReadFloat(input);

	return bounds;
}

static void TestNativeProjection(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(ReadWord(input) == 1000, "camera native projection extent must stay bounded at 1000");
	for (std::uint32_t case_id = 0; case_id != 1000; ++case_id) {
		const auto empty = ReadWord(input) != 0;
		sgre::scene::LayerCameraFrameState state;
		state.Viewport = {static_cast<std::int32_t>(ReadWord(input)), static_cast<std::int32_t>(ReadWord(input))};
		for (auto& value : state.View.Coefficients) {
			value = ReadFloat(input);
		}

		sgre::scene::MotionScissorMatrix parent;
		for (auto& value : parent.Coefficients) {
			value = ReadFloat(input);
		}

		state.RawScissor = ReadBounds(input);
		state.AppliedRoundedScissor = ReadBounds(input);
		const std::array<float, 4> original{state.RawScissor.MinimumX, state.RawScissor.MinimumY, state.RawScissor.MaximumX, state.RawScissor.MaximumY};
		sgre::scene::LayerCameraParameters camera;
		camera.Pivot = {ReadFloat(input), ReadFloat(input)};
		camera.Offset = {ReadFloat(input), ReadFloat(input)};
		camera.Scale = {ReadFloat(input), ReadFloat(input)};
		camera.RotationRadians = ReadFloat(input);
		camera.Clip = ReadBounds(input);
		std::array<std::uint32_t, 20> expected{};
		for (auto& word : expected) {
			word = ReadWord(input);
		}

		const auto count = ReadWord(input);
		Require(count <= 2, "native camera performs at most apply/restore scissor calls");
		std::vector<std::array<std::uint32_t, 4>> expected_submissions(count);
		for (auto& rectangle : expected_submissions) {
			for (auto& word : rectangle) {
				word = ReadWord(input);
			}
		}

		std::array<std::uint32_t, 4> final_applied{};
		for (auto& word : final_applied) {
			word = ReadWord(input);
		}

		std::vector<std::array<std::uint32_t, 4>> submissions;
		const std::span<const sgre::scene::MotionScissorMatrix> models = empty ? std::span<const sgre::scene::MotionScissorMatrix>{} : std::span<const sgre::scene::MotionScissorMatrix>{&parent, 1};
		sgre::scene::GraphicsModelStack actual_models(64);
		if (!empty) {
			actual_models.Push();
			actual_models.SetCurrent(parent);
		}
		sgre::scene::LayerCameraDrawScope scope(actual_models, state, [&](const auto& rectangle) {
			submissions.push_back({std::bit_cast<std::uint32_t>(rectangle.MinimumX), std::bit_cast<std::uint32_t>(rectangle.MinimumY), std::bit_cast<std::uint32_t>(rectangle.MaximumX), std::bit_cast<std::uint32_t>(rectangle.MaximumY)});
		});
		sgre::scene::ParallelTaskSet children(1);
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Draw = [&](auto, auto argument) {
			Require(argument == 0x87654321u && &scope.CurrentModel() == &actual_models.Current() && scope.Depth() == actual_models.Depth() && scope.Depth() == (empty ? 1u : 2u), "native child observes real model push and preserved draw argument");
			for (std::size_t index = 0; index != 12; ++index) {
				Require(std::bit_cast<std::uint32_t>(scope.CurrentModel().Coefficients[index]) == expected[index], "native camera composed matrix word differs in case " + std::to_string(case_id));
			}

			const auto& raw = scope.State().RawScissor;
			const auto& applied = scope.State().AppliedRoundedScissor;
			const std::array<float, 8> values{raw.MinimumX, raw.MinimumY, raw.MaximumX, raw.MaximumY, applied.MinimumX, applied.MinimumY, applied.MaximumX, applied.MaximumY};
			for (std::size_t index = 0; index != values.size(); ++index) {
				Require(std::bit_cast<std::uint32_t>(values[index]) == expected[index + 12], "native temporary raw/applied scissor word differs");
			}
		};
		static_cast<void>(children.Append(std::move(callbacks)));
		scope.Draw(camera, children, 0x87654321u);
		Require(submissions == expected_submissions && scope.Depth() == models.size(), "native physical scissor submission sequence or final depth differs");
		const auto& raw = scope.State().RawScissor;
		const std::array<float, 4> restored{raw.MinimumX, raw.MinimumY, raw.MaximumX, raw.MaximumY};
		for (std::size_t index = 0; index != restored.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(restored[index]) == std::bit_cast<std::uint32_t>(original[index]), "native raw scissor restoration differs");
		}
		if (!empty) {
			Require(scope.CurrentModel().Coefficients == parent.Coefficients, "native prior model contents must remain unchanged");
		}

		const auto& applied = scope.State().AppliedRoundedScissor;
		const std::array<float, 4> words{applied.MinimumX, applied.MinimumY, applied.MaximumX, applied.MaximumY};
		for (std::size_t index = 0; index != words.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(words[index]) == final_applied[index], "native restored applied cache differs");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native camera projection contains unchecked trailing data");
}

static void TestLiveStackNestingAndFailure() {
	sgre::scene::GraphicsModelStack models(3);
	models.Reset();
	sgre::scene::LayerCameraDrawScope frame(Frame(), models, {}, 3);
	sgre::scene::MotionScissorMatrix edited;
	edited.Coefficients[3] = 11;
	models.SetCurrent(edited);
	Require(&frame.CurrentModel() == &models.Current() && frame.CurrentModel().Coefficients[3] == 11, "existing live stack edit must reach camera without a snapshot");

	sgre::scene::ParallelTaskSet outer(1);
	sgre::scene::ParallelTaskSet inner(1);
	sgre::scene::LayerCameraParameters parameters;
	parameters.Offset = {7, 0};
	std::size_t draws = 0;
	sgre::scene::ParallelTaskCallbacks nested;
	nested.Draw = [&](auto, auto) {
		Require(models.Depth() == 3 && &frame.CurrentModel() == &models.Current() && models.Current().Coefficients[3] == 4, "nested actual task must observe the genuine composed model slot");
		++draws;
		RequireFailure([&] {
			frame.Draw({}, inner, 0);
		},
		    "capacity");
	};
	static_cast<void>(inner.Append(std::move(nested)));
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto argument) {
		Require(argument == 29 && models.Depth() == 2 && models.Current().Coefficients[3] == 4, "outer actual child must consume the current parent model edit");
		frame.Draw({}, inner, argument);
		Require(models.Depth() == 2 && models.Current().Coefficients[3] == 4, "nested live model must restore the enclosing actual slot");
		++draws;
		throw std::runtime_error("actual child failure");
	};
	static_cast<void>(outer.Append(std::move(child)));
	RequireFailure([&] {
		frame.Draw(parameters, outer, 29);
	},
	    "actual child failure");
	Require(draws == 2 && models.Depth() == 1 && models.Current().Coefficients == edited.Coefficients, "failed actual nested drawing must restore the original graphics owner depth and values");

	models.Pop();
	sgre::scene::ParallelTaskSet empty_parent(1);
	child = {};
	child.Draw = [&](auto, auto) {
		Require(models.Depth() == 1 && frame.CurrentModel().Coefficients[0] == 1, "live empty model owner must receive the native identity push");
	};
	static_cast<void>(empty_parent.Append(std::move(child)));
	frame.Draw({}, empty_parent, 0);
	Require(models.Depth() == 0, "empty actual model owner must remain empty after the camera scope");

	sgre::scene::ParallelTaskSet unbalanced(1);
	child = {};
	child.Draw = [&](auto, auto) {
		models.Pop();
	};
	static_cast<void>(unbalanced.Append(std::move(child)));
	RequireFailure([&] {
		frame.Draw({}, unbalanced, 0);
	},
	    "containing frame is invalid");
	Require(models.Depth() == 0, "unbalanced actual child rejection must not pop an absent parent slot");
}

static void TestClipModelPreflight() {
	std::uint32_t draws = 0;
	std::uint32_t submissions = 0;
	sgre::scene::LayerCameraDrawScope frame(Frame(), {}, [&](const auto&) {
		++submissions;
	});
	sgre::scene::ParallelTaskSet children(1);
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto argument) {
		Require(argument == 17, "disabled Clip must preserve the native draw argument");
		++draws;
	};
	static_cast<void>(children.Append(std::move(child)));
	RequireFailure([&] {
		frame.DrawClip({0, 0, 20, 20}, children, 17);
	},
	    "no current model");
	Require(draws == 0 && submissions == 0 && frame.Depth() == 0 && frame.State().RawScissor.MaximumX == 1280 && frame.State().AppliedRoundedScissor.MaximumY == 720, "empty enabled Clip rejection must precede all child, stack and scissor mutation");
	frame.DrawClip({1, 1, -1, -1}, children, 17);
	Require(draws == 1 && submissions == 0 && frame.Depth() == 0, "disabled Clip bypasses native model consumption and preserves an empty stack");
}

static void TestLiveSharedFrameStateAndNestedClip() {
	auto actual_state = Frame();
	sgre::scene::GraphicsModelStack actual_models(4);
	actual_models.Reset();
	std::vector<std::array<std::int32_t, 4>> packets;
	const auto submit = [&](const auto& rectangle) {
		packets.push_back({rectangle.MinimumX, rectangle.MinimumY, rectangle.MaximumX, rectangle.MaximumY});
	};
	sgre::scene::LayerCameraDrawScope outer(actual_models, actual_state, submit);
	sgre::scene::LayerCameraDrawScope inner(actual_models, actual_state, submit);
	Require(&outer.State() == &actual_state && &inner.State() == &actual_state, "both camera scopes must borrow the actual single graphics frame state");
	// Changes to enclosing fixture fields after scope construction must remain visible to both borrowers.
	actual_state.Viewport = {128, 64};
	actual_state.View.Coefficients = {2.0f / 128, 0, 0, -1, 0, -2.0f / 64, 0, 1, 0, 0, 1, 0};
	actual_state.RawScissor = {0, 0, 128, 64};
	actual_state.RoundedScissor = actual_state.RawScissor;
	actual_state.AppliedRoundedScissor = actual_state.RawScissor;
	sgre::scene::ParallelTaskSet outer_children(1);
	sgre::scene::ParallelTaskSet inner_children(1);
	std::size_t calls = 0;
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto argument) {
		Require(argument == 41 && actual_state.RawScissor.MinimumX == 16 && actual_state.RawScissor.MinimumY == 8 && actual_state.RawScissor.MaximumX == 64 && actual_state.RawScissor.MaximumY == 56, "nested independent clip scope must intersect the live outer scissor");
		++calls;
	};
	static_cast<void>(inner_children.Append(std::move(child)));
	child = {};
	child.Draw = [&](auto, auto argument) {
		Require(actual_state.RawScissor.MinimumX == 16 && actual_state.RawScissor.MaximumX == 112, "real outer child must observe the shared installed scissor");
		inner.DrawClip({0, 0, 64, 64}, inner_children, argument);
		Require(actual_state.RawScissor.MinimumX == 16 && actual_state.RawScissor.MaximumX == 112, "inner scope must restore the actual enclosing raw state");
		++calls;
	};
	static_cast<void>(outer_children.Append(std::move(child)));
	outer.DrawClip({16, 8, 112, 56}, outer_children, 41);
	const std::vector<std::array<std::int32_t, 4>> expected{{16, 8, 112, 56}, {16, 8, 64, 56}, {16, 8, 112, 56}, {0, 0, 128, 64}};
	Require(calls == 2 && packets == expected && actual_state.RawScissor.MaximumX == 128 && actual_state.AppliedRoundedScissor.MaximumX == 128 && actual_models.Depth() == 1, "shared frame/scissor state must preserve physical packet order and complete restoration");
}

static void TestLiveStatePreflightAndChildFailure() {
	auto actual_state = Frame();
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	std::size_t submits = 0;
	sgre::scene::LayerCameraDrawScope first(models, actual_state, [&](const auto&) {
		++submits;
	});
	sgre::scene::LayerCameraDrawScope second(models, actual_state, [&](const auto&) {
		++submits;
	});
	sgre::scene::ParallelTaskSet children(1);
	std::size_t calls = 0;
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto) {
		Require(second.State().RawScissor.MinimumX == 16 && second.State().AppliedRoundedScissor.MinimumX == 16, "other live camera must see the committed physical scissor before real child failure");
		++calls;
		throw std::runtime_error("shared frame child failure");
	};
	static_cast<void>(children.Append(std::move(child)));
	sgre::scene::LayerCameraParameters camera;
	camera.Clip = {16, 8, 112, 56};
	RequireFailure([&] {
		first.Draw(camera, children, 0);
	},
	    "shared frame child failure");
	Require(calls == 1 && submits == 2 && models.Depth() == 1 && second.State().RawScissor.MinimumX == 0 && actual_state.AppliedRoundedScissor.MaximumX == 1280, "failure must restore the common physical cache, raw state and real model owner");
	actual_state.View.Coefficients[0] = std::bit_cast<float>(std::uint32_t{0x7fc00001});
	RequireFailure([&] {
		second.Draw({}, children, 0);
	},
	    "finite coefficients");
	Require(calls == 1 && submits == 2 && models.Depth() == 1, "invalid live view must reject before any model push or child/device operation");
	actual_state.View = Frame().View;
	actual_state.Viewport.Width = 0;
	RequireFailure([&] {
		first.DrawClip({1, 1, -1, -1}, children, 0);
	},
	    "positive physical viewport");
	Require(calls == 1 && submits == 2 && models.Depth() == 1, "invalid actual viewport must reject before disabled-clip child dispatch");
}

int main(int argc, char** argv) {
	try {
		TestRealTaskCameraCompositionAndNesting();
		TestIntersectionFallbackAndFailureRestoration();
		TestMeasuredSafetyAndScaleStateConversion();
		TestScissorCacheAndSubmissionFailures();
		TestActualScaleViewCameraBoundary();
		TestClipModelPreflight();
		TestLiveStackNestingAndFailure();
		TestLiveSharedFrameStateAndNestedClip();
		TestLiveStatePreflightAndChildFailure();
		if (argc == 2) {
			TestNativeProjection(argv[1]);
		}

		std::cout << "Actual borrowed graphics stack and live shared frame/scissor state fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
