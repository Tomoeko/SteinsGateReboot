#include "sgre/scene/layer_scale_view.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

	throw std::runtime_error("expected failure: " + std::string(message));
}

static void TestTransferOwnershipOrderAndGeneration() {
	for (std::uint32_t selected = 0; selected != 3; ++selected) {
		sgre::scene::ParallelTaskSet source(3);
		sgre::scene::ParallelTaskSet destination(1);
		std::vector<std::uint32_t> events;
		std::array<sgre::scene::ParallelTaskHandle, 3> handles;
		for (std::uint32_t index = 0; index != 3; ++index) {
			sgre::scene::ParallelTaskCallbacks callbacks;
			callbacks.Release = [&, index](auto) {
				events.push_back(index);
			};
			handles[index] = source.Append(std::move(callbacks), {sgre::scene::TaskStatus::Complete, index == selected});
		}

		auto lease = source.Extract(handles[selected]);
		Require(events.empty() && !source.Contains(handles[selected]) && source.Count() == 2, "extract must unlink without releasing the concrete payload");
		Require(lease.State().Status == sgre::scene::TaskStatus::Complete && lease.State().DrawPaused, "extraction must preserve native status and pause bytes");
		const auto moved = destination.AppendLease(std::move(lease));
		Require(lease.IsEmpty() && moved.Owner != handles[selected].Owner && destination.State(moved).DrawPaused, "lease append changes registration owner without resurrecting the old identity");
		RequireFailure([&] {
			static_cast<void>(source.State(handles[selected]));
		},
		    "stale");
		source.Clear();
		destination.Clear();
		std::vector<std::uint32_t> expected;
		for (std::uint32_t index = 0; index != 3; ++index) {
			if (index != selected) {
				expected.push_back(index);
			}
		}

		expected.push_back(selected);
		Require(events == expected, "head/middle/tail extraction must preserve surviving list order and exact final disposal");
	}
}

static void TestLeaseFailureAndAbandonment() {
	sgre::scene::ParallelTaskSet source(1);
	sgre::scene::ParallelTaskSet full(1);
	const auto occupied = full.Append({});
	std::uint32_t releases = 0;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Release = [&](auto) {
		++releases;
	};
	auto lease = source.Extract(source.Append(std::move(callbacks)));
	RequireFailure([&] {
		static_cast<void>(full.AppendLease(std::move(lease)));
	},
	    "available capacity");
	Require(!lease.IsEmpty() && releases == 0, "capacity rejection must leave detached concrete ownership unchanged");
	full.SetStatus(occupied, sgre::scene::TaskStatus::Retire);
	full.Update();
	static_cast<void>(full.AppendLease(std::move(lease)));
	full.Clear();
	Require(releases == 1, "retry must release one payload only after its real destination owner retires");

	callbacks.Release = [&](auto) {
		++releases;
		throw std::runtime_error("lease cleanup failure");
	};
	{
		auto abandoned = source.Extract(source.Append(std::move(callbacks)));
		sgre::scene::ParallelTaskLease relocated(std::move(abandoned));
		Require(abandoned.IsEmpty() && !relocated.IsEmpty(), "lease move transfers one cleanup responsibility");
	}

	Require(releases == 2 && source.Count() == 0, "abandonment completes cleanup despite a failing release callback");
}

static void TestTransferRejectsTraversalBeforeMutation() {
	sgre::scene::ParallelTaskSet source(1);
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](auto handle) {
		Require(!source.CanExtract(handle), "active traversal must expose transfer safety boundary");
		RequireFailure([&] {
			static_cast<void>(source.Extract(handle));
		},
		    "stable boundary");
		Require(source.Contains(handle) && source.Count() == 1, "rejected transfer cannot unlink or release an executing task");
	};
	static_cast<void>(source.Append(std::move(callbacks)));
	source.Update();
}

static void TestLazyCameraAndDelayedLifetime() {
	sgre::scene::MotionLayerTasks parent(1);
	std::uint32_t releases = 0;
	sgre::scene::LayerScaleTaskCallbacks task;
	task.Release = [&] {
		++releases;
	};
	sgre::scene::LayerScaleView view(parent, std::move(task));
	view.SetOffset({-0.0f, 0});
	view.SetScale({1, 1});
	view.SetRotation(-0.0f);
	Require(!view.State().HasCamera && view.TaskState().DrawPaused, "default scale/offset/rotation retain one paused original First task");
	view.SetScale({std::bit_cast<float>(0x7fc00001u), 2});
	Require(view.State().HasCamera && releases == 0 && parent.Group(sgre::scene::MotionTaskGroup::First).Count() == 1, "NaN creates a camera by transferring the live task even when First was full");
	Require(std::bit_cast<std::uint32_t>(view.CameraState().Scale.X) == 0x7fc00001u, "camera setters preserve the original supplied float bits");
	view.SetVisible(true);
	view.Retire();
	Require(releases == 0 && view.TaskState().Status == sgre::scene::TaskStatus::Retire, "wrapper release marks task and camera without deleting either immediately");
	parent.Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(releases == 1 && parent.Group(sgre::scene::MotionTaskGroup::First).Count() == 0, "next First update releases the camera child before camera retirement");
	view.DetachParent();
	view.SetScale({3, 4});
	view.SetVisible(true);
	Require(view.State().Detached && !view.State().HasCamera, "detached guarded setters must not recreate parent/task ownership");
}

static void TestParentFirstAndUnsupportedDrawScope() {
	sgre::scene::MotionLayerTasks parent(2);
	std::uint32_t updates = 0;
	std::uint32_t draws = 0;
	std::uint32_t releases = 0;
	sgre::scene::LayerScaleTaskCallbacks task;
	task.Update = [&] {
		++updates;
	};
	task.Draw = [&](auto argument) {
		Require(argument == 0x80000001u, "camera child draw must preserve its original 32-bit argument");
		++draws;
	};
	task.Release = [&] {
		++releases;
	};
	sgre::scene::LayerScaleView view(parent, std::move(task));
	view.SetVisible(true);
	view.SetRotation(90);
	parent.Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(updates == 1, "camera update must dispatch the same concrete child's update");
	RequireFailure([&] {
		parent.Group(sgre::scene::MotionTaskGroup::First).Draw(0x80000001u);
	},
	    "matrix/scissor scope");
	Require(draws == 0, "missing camera transform must not silently draw an unscaled replacement");
	parent.Clear();
	Require(releases == 1 && !view.State().Detached, "parent owns resource release before borrowed wrapper detachment");
	view.DetachParent();
	RequireFailure([&] {
		static_cast<void>(view.TaskState());
	},
	    "live attached");

	sgre::scene::MotionLayerTasks scoped_parent(1);
	task.Draw = [&](auto) {
		++draws;
	};
	task.DrawWithCamera = [&](const auto& camera, auto& children, auto argument) {
		Require(camera.Offset.X == 7 && camera.Offset.Y == -2, "scope must receive actual portable camera fields");
		children.Draw(argument);
	};
	sgre::scene::LayerScaleView scoped(scoped_parent, std::move(task));
	scoped.SetOffset({7, -2});
	scoped.SetVisible(true);
	scoped_parent.Group(sgre::scene::MotionTaskGroup::First).Draw(0x80000001u);
	Require(draws == 1, "explicit camera scope must bracket the genuine task callback");
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(static_cast<char*>(static_cast<void*>(bytes.data())), bytes.size());
	if (!input) {
		throw std::runtime_error("native scale projection fixture is truncated");
	}

	return bytes[0] | (std::uint32_t{bytes[1]} << 8) | (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

static void TestNativeDrawingProxyPausePolicy() {
	sgre::scene::MotionLayerTasks parent(2);
	std::uint32_t draws = 0;
	sgre::scene::LayerScaleTaskCallbacks callbacks;
	callbacks.InitiallyDrawPaused = false;
	callbacks.Draw = [&](std::uint32_t argument) {
		Require(argument == 17, "DrawingProxy must preserve actual draw argument through lazy camera transfer");
		++draws;
	};
	callbacks.DrawWithCamera = [](const auto&, auto& children, std::uint32_t argument) {
		children.Draw(argument);
	};
	sgre::scene::LayerScaleView view(parent, std::move(callbacks));
	Require(!view.TaskState().DrawPaused, "native DrawingProxy must start unpaused instead of inheriting another payload's policy");
	parent.Group(sgre::scene::MotionTaskGroup::First).Draw(17);
	view.SetOffset({1, 0});
	parent.Group(sgre::scene::MotionTaskGroup::First).Draw(17);
	Require(draws == 2 && !view.TaskState().DrawPaused, "lazy camera transfer must preserve native unpaused leaf state");
	view.SetVisible(false);
	parent.Group(sgre::scene::MotionTaskGroup::First).Draw(17);
	Require(draws == 2, "explicit pause must remain independent of initial constructor policy");
}

static void TestNativeCameraProjection(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(ReadWord(input) == 1000, "native constructor vector count must remain bounded at 1000");
	for (std::uint32_t case_id = 0; case_id != 1000; ++case_id) {
		const auto count = ReadWord(input);
		const auto target = ReadWord(input);
		Require(count > 0 && count <= 8 && target < count, "native prepared list extent is invalid");
		sgre::scene::MotionLayerTasks parent(count);
		auto& first = parent.Group(sgre::scene::MotionTaskGroup::First);
		std::array<sgre::scene::ParallelTaskHandle, 8> initial;
		std::vector<std::uint32_t> releases;
		std::unique_ptr<sgre::scene::LayerScaleView> view;
		for (std::uint32_t index = 0; index != count; ++index) {
			if (index == target) {
				sgre::scene::LayerScaleTaskCallbacks callbacks;
				callbacks.Release = [&] {
					releases.push_back(0);
				};
				view = std::make_unique<sgre::scene::LayerScaleView>(parent, std::move(callbacks));
			} else {
				sgre::scene::ParallelTaskCallbacks callbacks;
				callbacks.Release = [&, index](auto) {
					releases.push_back(index + 1);
				};
				initial[index] = first.Append(std::move(callbacks));
			}
		}

		for (std::uint32_t step = 0; step != 8; ++step) {
			const auto method = ReadWord(input);
			const auto x_bits = ReadWord(input);
			const auto y_bits = ReadWord(input);
			const auto x = std::bit_cast<float>(x_bits);
			const auto y = std::bit_cast<float>(y_bits);
			switch (method) {
			case 0:
				view->SetOffset({x, y});
				break;
			case 1:
				view->SetScale({x, y});
				break;
			case 2:
				view->SetPrimaryScale(x);
				break;
			case 3:
				view->SetRotation(x);
				break;
			case 4:
				view->SetVisible(x_bits != 0);
				break;
			default:
				throw std::runtime_error("native setter projection method is invalid");
			}

			const auto expected_count = ReadWord(input);
			std::array<sgre::scene::ParallelTaskHandle, 8> order{};
			Require(first.WriteOrder(order) == expected_count, "native First-list count projection differs");
			for (std::uint32_t index = 0; index != expected_count; ++index) {
				const auto expected = ReadWord(input);
				if (expected == 255) {
					Require(view->State().HasCamera, "native camera-tail projection is missing");
				} else if (expected != 0) {
					Require(order[index] == initial[expected - 1], "native surviving task order projection differs");
				} else {
					Require(!view->State().HasCamera, "native raw First registration should not have reparented yet");
				}
			}

			const auto state = view->State();
			for (const auto value : {state.Scale.X, state.Scale.Y, state.Offset.X, state.Offset.Y}) {
				Require(std::bit_cast<std::uint32_t>(value) == ReadWord(input), "native scale wrapper cached float bits differ");
			}

			const auto camera_present = ReadWord(input) != 0;
			Require(state.HasCamera == camera_present, "native lazy camera selection differs");
			if (camera_present) {
				const auto camera = view->CameraState();
				for (const auto value : {camera.Offset.X, camera.Offset.Y, camera.Scale.X, camera.Scale.Y, camera.RotationRadians}) {
					Require(std::bit_cast<std::uint32_t>(value) == ReadWord(input), "native camera field float bits differ");
				}
			}

			Require(view->TaskState().DrawPaused == (ReadWord(input) != 0), "native actual task visibility differs");
			Require(releases.empty(), "native setter must preserve task/texture payload lifetime");
		}

		const auto parent_first = ReadWord(input) != 0;
		if (parent_first) {
			parent.Clear();
			view->DetachParent();
			view.reset();
		} else {
			view.reset();
			Require(releases.empty(), "wrapper-first release must defer actual disposal to First");
			parent.Clear();
		}

		const auto release_count = ReadWord(input);
		Require(releases.size() == release_count, "native concrete leaf release count differs");
		for (const auto actual : releases) {
			Require(actual == ReadWord(input), "native concrete leaf disposal sequence differs");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native projection has trailing unchecked data");
}

int main(int argc, char** argv) {
	try {
		TestTransferOwnershipOrderAndGeneration();
		TestLeaseFailureAndAbandonment();
		TestTransferRejectsTraversalBeforeMutation();
		TestLazyCameraAndDelayedLifetime();
		TestParentFirstAndUnsupportedDrawScope();
		TestNativeDrawingProxyPausePolicy();
		if (argc == 2) {
			TestNativeCameraProjection(argv[1]);
		}

		std::cout << "Layer scale view ownership and native projection fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
