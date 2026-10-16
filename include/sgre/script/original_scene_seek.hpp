#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sgre::script {

	struct OriginalSceneSeekTarget {
		std::string Storage;
		std::int32_t RequestedLine = 0;
		std::int32_t SnapshotLine = 0;
		std::int32_t Point = 0;
	};

	enum class OriginalSceneSeekPhase {
		Installing,
		WaitingForPlayer,
		Progressing,
		Arrived,
		Blocked,
		Cancelled,
		Failed
	};

	struct OriginalSceneSeekObservation {
		OriginalSceneSeekPhase Phase = OriginalSceneSeekPhase::Installing;
		std::string Storage;
		std::int32_t SourceLine = 0;
		std::int32_t Point = 0;
		std::string Reason;
		std::string Source;
		std::string Function;
		std::uint32_t ScriptLine = 0;
		std::size_t Snapshots = 0;
		std::size_t MovieRequests = 0;
		std::size_t MovieConfirmations = 0;
	};

	struct OriginalSceneSeekLimits {
		std::size_t MaximumPolls = 54000;
		std::size_t MaximumSnapshots = 1000000;
		std::size_t MaximumMovieRequests = 64;
		std::size_t MaximumConfirmationPolls = 600;
	};

	class OriginalSceneSeek {
	public:
		// Explicit per-launch policy. The VM outlives this owner. Target coordinates come from
		// the real scenario catalog's CanonicalStorage; original scripts execute every prior command.
		// The owned semantic force_skip_all query leaves the original key0 binding and OS input intact.
		OriginalSceneSeek(SquirrelVm& vm, OriginalSceneSeekTarget target, OriginalSceneSeekLimits limits = {});
		~OriginalSceneSeek();
		OriginalSceneSeek(const OriginalSceneSeek&) = delete;
		OriginalSceneSeek& operator=(const OriginalSceneSeek&) = delete;
		// Call after original module publication, before MyApplication/MyPlayer derive or lock.
		void InstallAvailableHooks();
		// Call at the idle script Update boundary. Reports yielded original choice/phone frames;
		// it never chooses a branch, resumes a VM context, or dispatches a subset of scenario tags.
		void Poll();
		// Release the owned action immediately; original checkSkipStop consumes release normally.
		// No VM call is made, including during shutdown. Arrival invokes the original release method.
		void Cancel() noexcept;
		[[nodiscard]] const OriginalSceneSeekObservation& Observation() const noexcept;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
