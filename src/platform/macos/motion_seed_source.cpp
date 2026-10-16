#include "sgre/platform/motion_seed_source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include <Security/SecRandom.h>

namespace sgre::platform {

	static std::array<std::uint16_t, 3> ReadMotionSeedEntropy() {
		std::array<std::uint8_t, 6> bytes{};
		const auto status = SecRandomCopyBytes(kSecRandomDefault, bytes.size(), bytes.data());
		if (status != errSecSuccess) {
			throw std::runtime_error("macOS Motion seed entropy failed with Security status " + std::to_string(status));
		}

		std::array<std::uint16_t, 3> halfwords{};
		for (std::size_t index = 0; index < halfwords.size(); ++index) {
			halfwords[index] = static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[index * 2]) | (static_cast<std::uint16_t>(bytes[index * 2 + 1]) << 8));
		}

		return halfwords;
	}

	MacMotionSeedSource::MacMotionSeedSource(core::LooperFrame& actual_looper, core::ThreadTaskFrame& actual_workers, const HostPerformanceCounter& actual_counter) : m_Looper(actual_looper), m_Workers(actual_workers), m_Counter(actual_counter), m_Seed(actual_counter.ReadRawCounter()) {
		const core::LooperContainingMutation mutation(m_Looper, m_Workers);
		m_Mutation = &mutation;
		RegisterClock();
		m_Mutation = nullptr;
	}

	MacMotionSeedSource::~MacMotionSeedSource() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void MacMotionSeedSource::RequireOpen() const {
		if (std::this_thread::get_id() != m_Thread || m_Closed || m_Retiring || !m_Registered || !m_Looper.Group(core::LooperFrameGroup::VSync).Contains(m_Clock)) {
			throw std::runtime_error("macOS Motion seed source requires its live serialized owner and exact VSync registration");
		}
	}

	void MacMotionSeedSource::RegisterClock() {
		m_Mutation->RequireOwner(m_Looper, m_Workers);
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [this](auto actual_clock) {
			UpdateClock(actual_clock);
		};
		callbacks.CanRelease = [this](auto actual_clock) {
			return CanReleaseClock(actual_clock);
		};
		callbacks.Release = [this](auto actual_clock) {
			ReleaseClock(actual_clock);
		};
		// Append owns its physical capacity check. Unrelated clocks need not consent to release.
		m_Clock = m_Looper.Group(core::LooperFrameGroup::VSync).Append(std::move(callbacks));
		m_Registered = true;
	}

	void MacMotionSeedSource::UpdateClock(scene::ParallelTaskHandle actual_clock) {
		RequireOpen();
		if (actual_clock != m_Clock || m_Updating || m_Mutation != nullptr) {
			throw std::runtime_error("macOS Motion seed update requires its exact nonrecursive live VSync clock");
		}

		m_Updating = true;
		try {
			const auto counter = m_Counter.ReadRawCounter();
			// This documented port input replaces unwritten native stack residue, never captured native bytes.
			const auto platform_entropy = ReadMotionSeedEntropy();
			m_Seed.MixHookInput(counter, platform_entropy);
			m_Updating = false;
		} catch (...) {
			m_Updating = false;
			throw;
		}
	}

	bool MacMotionSeedSource::CanReleaseClock(scene::ParallelTaskHandle actual_clock) const noexcept {
		try {
			if (std::this_thread::get_id() != m_Thread || !m_Retiring || m_Updating || m_Mutation == nullptr || !m_Registered || actual_clock != m_Clock) {
				return false;
			}

			m_Mutation->RequireOwner(m_Looper, m_Workers);

			return true;
		} catch (...) {
			return false;
		}
	}

	void MacMotionSeedSource::ReleaseClock(scene::ParallelTaskHandle actual_clock) {
		if (!CanReleaseClock(actual_clock)) {
			throw std::runtime_error("macOS Motion seed clock release requires its exact containing retirement scope");
		}

		m_Registered = false;
	}

	const scene::MotionSeedBuffer& MacMotionSeedSource::SeedBuffer() const {
		RequireOpen();

		return m_Seed;
	}

	bool MacMotionSeedSource::IsClosed() const noexcept {
		return m_Closed;
	}

	void MacMotionSeedSource::Close() {
		if (std::this_thread::get_id() != m_Thread) {
			throw std::runtime_error("macOS Motion seed retirement requires its serialized owner thread");
		}
		if (m_Closed) {
			return;
		}

		RequireOpen();
		if (m_Updating) {
			throw std::runtime_error("macOS Motion seed retirement cannot reenter its active entropy update");
		}

		const core::LooperContainingMutation mutation(m_Looper, m_Workers);
		m_Mutation = &mutation;
		m_Retiring = true;
		try {
			auto& group = m_Looper.Group(core::LooperFrameGroup::VSync);
			if (!group.CanExtract(m_Clock)) {
				throw std::runtime_error("macOS Motion seed exact VSync clock cannot retire at this containing boundary");
			}

			auto clock = group.Extract(m_Clock);
			clock.Release();
			m_Closed = true;
		} catch (...) {
			m_Retiring = false;
			m_Mutation = nullptr;
			throw;
		}
		m_Retiring = false;
		m_Mutation = nullptr;
	}

} // namespace sgre::platform
