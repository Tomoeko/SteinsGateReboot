#include "sgre/render/presentation_synchronization.hpp"

#include "presentation_system_boundary.hpp"
#include "sgre/core/looper_frame.hpp"
#include "sgre/render/sprite_presentation.hpp"
#include "sprite_presentation_fixture.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace sgre::render {

	class PresentationSynchronizationFixtureBridge {
	public:
		[[nodiscard]] static std::array<std::uint32_t, 2> Words(const PresentationSynchronization& synchronization) {
			return {synchronization.m_Current, synchronization.m_Requested};
		}
	};

} // namespace sgre::render

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Work>
static void RequireFailure(Work work, std::string_view message) {
	try {
		work();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected authored presentation failure: " + std::string(message));
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 16;
	result.MaxIndices = 16;
	result.MaxDraws = 4;

	return result;
}

static void RequireHidden(const sgre::render::SpritePassPresentation& presentation, std::int32_t foreground) {
	const auto state = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(!state.Presenting && !state.ApplicationActive && !state.WindowVisible && !state.WindowKey && !state.WindowMain && state.ViewAttached && state.LayerAttached, "actual presentation remained active, detached or visible");
	Require(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "presentation changed the foreground application");
}

static void RequireClosed(const sgre::render::SpritePresentationFixtureObjects& objects) {
	Require(objects.Window == nil || (!objects.Window.visible && !objects.Window.keyWindow && objects.Window.contentView == nil && ![NSApp.windows containsObject:objects.Window]), "retired presentation retained its actual window");
	Require(objects.View == nil || (objects.View.layer == nil && objects.View.window == nil), "retired presentation retained its attached view");
	Require(NSApp.windows.count == 0 && !NSApp.active, "retired presentation left an active application/window");
}

static void TestFirstCause() {
	sgre::render::PresentationSystemBoundary boundary;
	RequireFailure([&] {
		boundary.Invoke(sgre::render::PresentationSystemOperation::Transfer, [&] {
			try {
				boundary.Invoke(sgre::render::PresentationSystemOperation::Commit, [] {
					throw std::runtime_error("authored inner C++ first cause");
				});
			} catch (const std::runtime_error&) {
				[NSException raise:@"AuthoredOuterFailure" format:@"authored outer Objective-C second cause"];
			}
		});
	},
	    "authored inner C++ first cause");
	Require(boundary.Operation() == sgre::render::PresentationSystemOperation::Commit && boundary.Kind() == sgre::render::PresentationSystemFailureKind::Cpp, "nested Objective-C failure replaced the first C++ kind/operation");
	Require(!boundary.Cleanup(sgre::render::PresentationSystemOperation::Policy, [] {
		[NSException raise:@"AuthoredCleanupFailure" format:@"authored cleanup third cause"];
	}),
	    "authored cleanup exception was not retained");
	Require(boundary.CleanupFailed() && boundary.Operation() == sgre::render::PresentationSystemOperation::Commit && boundary.Kind() == sgre::render::PresentationSystemFailureKind::Cpp, "cleanup replaced the retained original cause");
	RequireFailure([&] {
		boundary.RequireOperational();
	},
	    "authored inner C++ first cause");
}

enum class FailureCase {
	Happy,
	PolicyAfter,
	PolicyRollbackAfter,
	PolicyRollbackBefore,
	TransferAfter,
	CommitBefore,
	CommitAfter
};

static void TestActual(sgre::core::LooperFrame& looper, const std::filesystem::path& sprite_shader, const std::filesystem::path& transfer_shader, FailureCase selected, std::int32_t foreground) {
	const auto description = Description();
	sgre::render::SpritePassRenderer renderer(description, sprite_shader);
	sgre::render::SpritePassRenderer sibling(description, renderer);
	const std::array<float, 16> projection{};
	static_cast<void>(renderer.Submit({}, projection, {0, 0, 1, 1}));
	sgre::render::SpritePresentationFixtureObjects closed;
	std::array<sgre::render::SpritePresentationFixtureCommands, 3> retained{};
	@autoreleasepool {
		auto presentation = std::make_unique<sgre::render::SpritePassPresentation>(renderer, transfer_shader);
		closed = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(*presentation);
		{
			sgre::render::PresentationSynchronization synchronization(looper, *presentation);
			synchronization.Request(0);
			std::size_t policy_changes = 0;
			sgre::render::SpritePassPresentationFixtureBridge::SetSystemHook(*presentation, [&](auto operation, auto moment) {
				if (selected == FailureCase::PolicyRollbackBefore && operation == sgre::render::PresentationSystemOperation::Policy && moment == sgre::render::PresentationFixtureMoment::Before && policy_changes == 1) {
					throw std::runtime_error("authored rollback failure before actual setter");
				}
				if (operation == sgre::render::PresentationSystemOperation::Policy && moment == sgre::render::PresentationFixtureMoment::After) {
					++policy_changes;
					if ((selected == FailureCase::PolicyAfter || selected == FailureCase::PolicyRollbackAfter || selected == FailureCase::PolicyRollbackBefore) && policy_changes == 1) {
						[NSException raise:@"AuthoredPolicyFailure" format:@"authored policy first cause after actual setter"];
					}
					if (selected == FailureCase::PolicyRollbackAfter && policy_changes == 2) {
						throw std::runtime_error("authored rollback second cause after actual setter");
					}
				}
				if (selected == FailureCase::TransferAfter && operation == sgre::render::PresentationSystemOperation::Transfer && moment == sgre::render::PresentationFixtureMoment::After) {
					throw std::runtime_error("authored transfer failure with actual active encoder");
				}
				if (selected == FailureCase::CommitBefore && operation == sgre::render::PresentationSystemOperation::Commit && moment == sgre::render::PresentationFixtureMoment::Before) {
					[NSException raise:@"AuthoredCommitBefore" format:@"authored failure before actual commit"];
				}
				if (selected == FailureCase::CommitAfter && operation == sgre::render::PresentationSystemOperation::Commit && moment == sgre::render::PresentationFixtureMoment::After) {
					throw std::runtime_error("authored failure after actual commit");
				}
			});
			if (selected == FailureCase::Happy) {
				Require(presentation->TryPresentCurrent(synchronization, true) == sgre::render::SpritePresentationStatus::QueuedHidden, "happy path requires one genuine available hidden drawable and committed transfer");
				Require(synchronization.Current() == 0 && !closed.Layer.displaySyncEnabled, "real queued transfer did not adopt its request and actual unsynchronized policy");
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (!presentation->PollTransfers()) {
					Require(std::chrono::steady_clock::now() < deadline, "actual hidden transfer exceeded its bounded retirement");
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			} else {
				const auto message = selected == FailureCase::PolicyAfter || selected == FailureCase::PolicyRollbackAfter || selected == FailureCase::PolicyRollbackBefore ? "authored policy first cause" : selected == FailureCase::TransferAfter ? "authored transfer failure"
				    : selected == FailureCase::CommitBefore                                                                                                                                                                                         ? "authored failure before actual commit"
				                                                                                                                                                                                                                                    : "authored failure after actual commit";
				RequireFailure([&] {
					static_cast<void>(presentation->TryPresentCurrent(synchronization, true));
				},
				    message);
				const auto state = sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation);
				const auto words = sgre::render::PresentationSynchronizationFixtureBridge::Words(synchronization);
				Require(words == std::array<std::uint32_t, 2>{1, 0}, "failed transfer fabricated synchronization adoption");
				const bool restored = selected != FailureCase::PolicyRollbackBefore;
				Require(!state.Presenting && state.DisplaySynchronized == restored && static_cast<bool>(closed.Layer.displaySyncEnabled) == restored, "failure phase or actual/cached policy disagrees with its real cleanup result");
				Require(state.CleanupFailed == (selected == FailureCase::PolicyRollbackAfter || selected == FailureCase::PolicyRollbackBefore), "actual cleanup failure tracking differs");
				Require(state.FailureKind == (selected == FailureCase::PolicyAfter || selected == FailureCase::PolicyRollbackAfter || selected == FailureCase::PolicyRollbackBefore || selected == FailureCase::CommitBefore ? sgre::render::PresentationSystemFailureKind::ObjectiveC : sgre::render::PresentationSystemFailureKind::Cpp), "actual boundary retained the wrong first exception kind");
				Require(state.FailedOperation == (selected == FailureCase::PolicyAfter || selected == FailureCase::PolicyRollbackAfter || selected == FailureCase::PolicyRollbackBefore ? sgre::render::PresentationSystemOperation::Policy : selected == FailureCase::TransferAfter ? sgre::render::PresentationSystemOperation::Transfer
				                                                                                                                                                                                                                                                                     : sgre::render::PresentationSystemOperation::Commit),
				    "actual boundary retained the wrong first operation");
				RequireFailure([&] {
					static_cast<void>(presentation->PollTransfers());
				},
				    message);
				RequireFailure([&] {
					static_cast<void>(presentation->Geometry());
				},
				    message);
				RequireFailure([&] {
					static_cast<void>(presentation->CurrentSubmission());
				},
				    message);
				Require(renderer.CanSubmit() == (selected != FailureCase::CommitAfter) && sibling.CanSubmit() == (selected != FailureCase::CommitAfter), "commit ambiguity did not fence only its actual shared GPU domain");
				retained = sgre::render::SpritePassPresentationFixtureBridge::RetainCommands(*presentation);
				if (selected == FailureCase::CommitBefore || selected == FailureCase::CommitAfter) {
					Require(retained[0].Transfer != nil, "commit fixture did not retain its actual command");
					Require(selected == FailureCase::CommitBefore ? retained[0].Transfer.status < MTLCommandBufferStatusCommitted : retained[0].Transfer.status >= MTLCommandBufferStatusCommitted, "actual command status does not match the authored before/after boundary");
				} else {
					Require(state.RetainedTransfers == 0, "precommit transfer failure left a published slot");
				}
			}
			RequireHidden(*presentation, foreground);
		}
		presentation.reset();
	}
	RequireClosed(closed);
	if (selected == FailureCase::CommitAfter) {
		Require(retained[0].Transfer.status == MTLCommandBufferStatusCompleted, "destructor did not wait for its genuine committed transfer");
	} else if (selected == FailureCase::CommitBefore) {
		Require(retained[0].Transfer.status < MTLCommandBufferStatusCommitted, "destructor committed or waited for unsubmitted fixture work");
	}
	Require(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "presentation teardown changed the foreground application");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "presentation failure fixture requires both shader paths");
		@autoreleasepool {
			Require([NSThread isMainThread], "actual hidden fixture requires main-thread entry");
			const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
			TestFirstCause();
			sgre::core::ThreadTaskFrame workers([] {
				return std::int64_t{0};
			},
			    1);
			bool retired = false;
			sgre::core::LooperTaskOperations task;
			task.ReadState = [] {
				return sgre::core::LooperTaskState{};
			};
			task.Update = [] {
				throw std::runtime_error("leaf fixture cannot replace original root Update");
			};
			task.Draw = [](std::uint32_t) {
				throw std::runtime_error("leaf fixture cannot replace original root Draw");
			};
			task.CanRetire = [&] {
				return !retired;
			};
			task.Retire = [&] {
				retired = true;
			};
			sgre::core::LooperFrame looper(workers, std::move(task), [](auto&) {
				throw std::runtime_error("leaf fixture cannot replace actual application pump");
			},
			    0);
			try {
				for (const auto selected : {FailureCase::Happy, FailureCase::PolicyAfter, FailureCase::PolicyRollbackAfter, FailureCase::PolicyRollbackBefore, FailureCase::TransferAfter, FailureCase::CommitBefore, FailureCase::CommitAfter}) {
					TestActual(looper, argv[1], argv[2], selected, foreground);
				}
			} catch (...) {
				looper.ReleaseTask();
				looper.Close();
				workers.Close();
				throw;
			}
			looper.ReleaseTask();
			looper.Close();
			workers.Close();
			Require(retired && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "actual containing fixture cleanup or foreground changed");
			std::cout << "PASS nested first-cause preservation; seven actual hidden presentation cases; real policy rollback; active encoder cleanup; uncommitted skip/committed drain; shared commit fence; no window activation or audio\n";
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
