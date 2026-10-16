#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::script {

	struct OriginalCheckpointLocation {
		std::string Storage;
		std::string Label;
		std::int32_t SourceLine = 0;
		std::int32_t Point = 0;
		std::int32_t Record = 0;
		bool operator==(const OriginalCheckpointLocation&) const = default;
	};

	// Authored cache format for the complete original getSaveData primitive tree.
	// This is neither the original encrypted save format nor a scene-only snapshot.
	[[nodiscard]] std::vector<std::uint8_t> EncodeOriginalCheckpoint(SquirrelVm& vm, const SquirrelValue& data, const OriginalCheckpointLocation& location, std::string_view identity, std::string_view parent);
	[[nodiscard]] SquirrelValue RestoreOriginalCheckpoint(SquirrelVm& vm, std::span<const std::uint8_t> bytes, const OriginalCheckpointLocation& expected, std::string_view identity);
	[[nodiscard]] std::string OriginalCheckpointKey(const OriginalCheckpointLocation& location);

	class OriginalCheckpointRecorder {
	public:
		// The sink receives owned bytes and must queue, rather than perform, disk I/O.
		// Its returned content identity records the authentic preceding checkpoint.
		using Sink = std::function<std::string(const OriginalCheckpointLocation&, std::vector<std::uint8_t>)>;
		OriginalCheckpointRecorder(SquirrelVm& vm, std::string identity, std::string parent, Sink sink);
		~OriginalCheckpointRecorder();
		OriginalCheckpointRecorder(const OriginalCheckpointRecorder&) = delete;
		OriginalCheckpointRecorder& operator=(const OriginalCheckpointRecorder&) = delete;
		void InstallAvailableHook();
		void Cancel() noexcept;
		void SetEntryPredecessor(std::string digest);
		[[nodiscard]] std::size_t Captures() const noexcept;
		[[nodiscard]] std::string_view Error() const noexcept;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
