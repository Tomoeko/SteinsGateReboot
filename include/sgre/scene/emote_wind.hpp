#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace sgre::scene {
	inline constexpr std::size_t kEmoteWindBands = 128;

	// One application owns this stream. Every actual Emote borrows it across managers.
	class EmoteWindRandom {
	public:
		explicit EmoteWindRandom(std::function<std::int64_t()> read_unix_seconds);
		EmoteWindRandom(const EmoteWindRandom&) = delete;
		EmoteWindRandom& operator=(const EmoteWindRandom&) = delete;
		[[nodiscard]] float NextUnitFloat();
		[[nodiscard]] std::optional<std::array<std::uint32_t, 4>> State() const;

	private:
		std::function<std::int64_t()> m_ReadUnixSeconds;
		std::optional<std::array<std::uint32_t, 4>> m_State;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_Seeding = false;
	};

	struct EmoteWindParameters {
		float Start = 0;
		float Goal = 0;
		float Speed = 0;
		float Minimum = 0;
		float Maximum = 0;
	};
	struct EmoteWindBand {
		bool Active = false;
		// Native inactive numeric cells are uninitialized and never observed.
		float Position = 0;
		float Strength = 0;
	};
	struct EmoteWindControlState {
		float Start = 0;
		float Goal = 0;
		bool Enabled = false;
		float Minimum = 1;
		float Maximum = 0;
		float SignedVelocity = 0;
		float BirthClock = 0;
		std::array<EmoteWindBand, kEmoteWindBands> Bands{};
	};
	struct EmoteWindSnapshot {
		EmoteWindParameters Parameters;
		std::optional<EmoteWindControlState> Control;
	};

	class EmoteWindOwner {
	public:
		// The optional stream must outlive this owner; a nonempty Start requires it without seeding it.
		EmoteWindOwner(float metadata_scale, EmoteWindRandom* shared_random);
		void Start(EmoteWindParameters parameters);
		void Stop();
		void Advance(float elapsed_ticks);
		[[nodiscard]] float Sample(float position) const;
		[[nodiscard]] EmoteWindSnapshot Snapshot() const;

	private:
		void RequireIdle() const;
		float m_MetadataScale;
		EmoteWindRandom* m_Random;
		EmoteWindParameters m_Parameters;
		std::unique_ptr<EmoteWindControlState> m_Control;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_Operating = false;
	};
} // namespace sgre::scene
