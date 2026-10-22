#pragma once

#include "sgre/script/original_checkpoint.hpp"
#include "sgre/script/original_scene_seek.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>

namespace sgre::debug {

	// Explicit headless tooling only. The stream outlives this serialized owner;
	// receipts observe original save/coroutine boundaries, never dispatch scenario commands.
	class GameTestTrace {
	public:
		explicit GameTestTrace(std::ostream& output);
		void Begin(std::string_view identity, std::string_view parent);
		void Snapshot(const script::OriginalCheckpointLocation& location, std::string_view digest);
		void Seek(const script::OriginalSceneSeekObservation& observation);
		void ScriptOutput(std::string_view message, bool error);
		void Frame(std::uint64_t updates, std::uint64_t draws);
		void Boundary(std::string_view kind, std::string_view reason);

	private:
		std::ostream& m_Output;
		std::size_t m_Sequence = 0;
		std::size_t m_Bytes = 0;
		void Emit(std::string_view event, std::string_view fields);
	};

} // namespace sgre::debug
