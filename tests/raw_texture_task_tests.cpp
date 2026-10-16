#include "raw_texture_fixture.hpp"
#include "sgre/scene/raw_image.hpp"
#include "sgre/scene/raw_texture_task.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected raw task safety failure");
}

static void TestFixedLogicalExtentAndDrawState() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::RawTextureTask task(backend, {2.75f, 3.5f});
	Require(backend.Created.Width == 2 && backend.Created.Height == 3 && backend.Created.OriginalFormat == 87, "GPU extents truncate while logical extents remain exact floats");
	Require(task.State().Task.DrawPaused && task.State().Opacity == 255 && task.State().PackedRgb == 0xffffff00, "fixed-size view starts hidden with white full opacity");
	task.SetCoordinate({-2.5f, 1.25f});
	task.SetColor({-0.5f, 0.25f, 1.0f});
	task.SetOpacity(0x12340080);
	task.SetDrawPaused(false);
	const auto packet = task.PrepareDrawPacket();
	Require(packet.Color == 0x80ff3fff && packet.Vertices[0].Position.X == -2.5f && packet.Vertices[0].Position.Y == 1.25f, "draw uses low-byte opacity with recovered unsigned color conversion");
	Require(packet.Vertices[3].Position.X == 0.25f && packet.Vertices[3].Position.Y == 4.75f && packet.Vertices[3].Uv.X == 1 && packet.Vertices[3].Uv.Y == 1, "four-vertex packet preserves fractional logical size and full UV extent");
	Require(packet.TextureAttributes == 0, "nonwhite draw color selects recovered texture attributes zero");
}

static void TestRestoreAndDeferredUpload() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::RawTextureTask task(backend, {2, 1}, "8888_RGBA");
	sgre::scene::RawImage image(2, 1);
	image.RestorePixels(std::array<std::uint8_t, 8>{10, 20, 30, 40, 50, 60, 70, 80});
	image.ClearUpdated();
	task.RestoreImage(image);
	Require(!task.Texture().State().Locked && task.Texture().State().Dirty && backend.UploadCount == 0 && !image.IsUpdated(), "restore unlocks dirty shadow without uploading or changing the image update flag");
	task.Texture().Bind(0, sgre::scene::MutableTextureStage::Pixel);
	Require(backend.Uploaded == std::vector<std::byte>{std::byte{30}, std::byte{20}, std::byte{10}, std::byte{40}, std::byte{70}, std::byte{60}, std::byte{50}, std::byte{80}}, "native image restore copies BGRA even when requested physical format is RGBA");
	Require(backend.UploadCount == 1 && !task.Texture().State().Dirty && backend.BindCount == 1, "actual owner bind performs one deferred upload and clears dirty state");
	task.RestoreRgb(std::array<std::uint8_t, 6>{1, 2, 3, 4, 5, 6});
	task.Texture().Bind(0, sgre::scene::MutableTextureStage::Pixel);
	Require(backend.Uploaded == std::vector<std::byte>{std::byte{3}, std::byte{2}, std::byte{1}, std::byte{255}, std::byte{6}, std::byte{5}, std::byte{4}, std::byte{255}}, "RGB restoration expands into actual BGRA shadow with opaque alpha");
	RequireFailure([&] {
		task.RestoreRgb(std::array<std::uint8_t, 5>{});
	},
	    "byte count");
	Require(!task.Texture().State().Dirty && !task.Texture().State().Locked, "bad byte count fails before changing shadow map/dirty state");
}

static void TestFractionalImageCopyIsNativeNoop() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::RawTextureTask task(backend, {2.25f, 1});
	sgre::scene::RawImage image(2, 1);
	task.RestoreImage(image);
	Require(!task.Texture().State().Dirty && !task.Texture().State().Locked, "native helper skips image copies whose exact float logical extent differs");
	task.RestoreRgb(std::array<std::uint8_t, 6>{1, 2, 3, 4, 5, 6});
	Require(task.Texture().State().Dirty, "RGB frontend uses integer extents separately from exact image helper guard");
}

static void TestEmbeddedUpdateOrderAndPausedTask() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::RawTextureTask task(backend, {1, 1});
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](sgre::scene::SerialTask& receiver) {
		RequireFailure([&] {
			task.Close();
		},
		    "during its process update");
		Require(backend.Live && !task.State().Closed, "rejected update-time close preserves actual texture and task ownership");
		backend.Events.push_back("serial-update");
		receiver.SetStatus(sgre::scene::TaskStatus::Retire);
	};
	serial.Release = [&] {
		backend.Events.push_back("serial-release");
	};
	static_cast<void>(task.SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Update = [&](sgre::scene::ParallelTaskHandle) {
		backend.Events.push_back("parallel-update");
	};
	static_cast<void>(task.ParallelTasks().Append(std::move(parallel)));
	task.Update();
	Require(backend.Events == std::vector<std::string>{"serial-update", "serial-release", "parallel-update"}, "serial completes before parallel update, and drawing pause does not pause updates");
	Require(task.SerialTasks().Status() == sgre::scene::TaskStatus::Active, "embedded empty serial root retains active status");
	task.SetStatus(static_cast<sgre::scene::TaskStatus>(19));
	task.Update();
	Require(backend.Events.size() == 3, "generic native dispatch skips every nonzero task status");
}

static void TestTextureBeforeTaskRetirementAndFailure() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::RawTextureTask task(backend, {1, 1});
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Release = [&](sgre::scene::ParallelTaskHandle) {
		Require(!backend.Live && task.State().Closed, "parallel destructor observes actual texture already retired");
		backend.Events.push_back("parallel");
		throw std::runtime_error("fixture release failure");
	};
	static_cast<void>(task.ParallelTasks().Append(std::move(parallel)));
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		Require(!backend.Live && task.State().Closed, "serial destructor observes texture retirement before its group");
		backend.Events.push_back("serial");
	};
	static_cast<void>(task.SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	RequireFailure([&] {
		task.Close();
	},
	    "fixture release failure");
	Require(backend.Events == std::vector<std::string>{"view", "texture", "parallel", "serial"}, "actual view/texture/parallel/serial owners retire in recovered order despite failure");
	task.Close();
	Require(backend.Events.size() == 4, "explicit Close retires each owner once");
	RequireFailure([&] {
		static_cast<void>(task.Texture());
	},
	    "retired");
}

static void TestBorrowedGroupCloseRejectsBeforeMutation() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::RawTextureTask task(backend, {1, 1});
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](sgre::scene::SerialTask&) {
		Require(!task.CanClose() && !task.SerialTasks().CanClear(), "borrowed serial update exposes unsafe composite retirement");
		RequireFailure([&] {
			task.Close();
		},
		    "task callback");
		Require(backend.Live && !task.State().Closed, "rejected borrowed-group close preserves the texture and all task ownership");
	};
	static_cast<void>(task.SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	task.SerialTasks().Update();
	Require(task.CanClose() && task.SerialTasks().CanClear(), "callback completion restores ordinary retirement availability");
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Draw = [&](sgre::scene::ParallelTaskHandle, std::uint32_t) {
		Require(!task.CanClose() && !task.ParallelTasks().CanClear(), "borrowed parallel drawing also guards composite retirement");
		RequireFailure([&] {
			task.Close();
		},
		    "task callback");
	};
	static_cast<void>(task.ParallelTasks().Append(std::move(parallel)));
	task.ParallelTasks().Draw(0);
	task.Close();
	Require(!backend.Live && task.State().Closed, "retirement succeeds once both borrowed group callbacks return");
}

static void TestCreationDomainAndNativeCString() {
	for (const auto invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 16385.0f}) {
		sgre::scene::tests::TaskFixtureBackend backend;
		RequireFailure([&] {
			sgre::scene::RawTextureTask task(backend, {invalid, 1});
		},
		    "extents");
		Require(!backend.Live && backend.Events.empty(), "invalid extent fails before texture creation");
	}

	sgre::scene::tests::TaskFixtureBackend backend;
	RequireFailure([&] {
		sgre::scene::RawTextureTask task(backend, {1, 1}, "unknown");
	},
	    "four-format");
	sgre::scene::RawTextureTask task(backend, {1, 1}, std::string_view("8888_BGRX\0ignored", 17));
	Require(backend.Created.OriginalFormat == 88, "native strcmp interprets color mode only through its first NUL");
}

int main() {
	try {
		TestFixedLogicalExtentAndDrawState();
		TestRestoreAndDeferredUpload();
		TestFractionalImageCopyIsNativeNoop();
		TestEmbeddedUpdateOrderAndPausedTask();
		TestTextureBeforeTaskRetirementAndFailure();
		TestBorrowedGroupCloseRejectsBeforeMutation();
		TestCreationDomainAndNativeCString();
		std::cout << "raw task ownership, pixel restore, update and retirement fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
