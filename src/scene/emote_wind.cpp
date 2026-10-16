#include "sgre/scene/emote_wind.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	static constexpr float kMaximumWindBirthTrials = 65536;

	EmoteWindRandom::EmoteWindRandom(std::function<std::int64_t()> read_unix_seconds) : m_ReadUnixSeconds(std::move(read_unix_seconds)) {
		if (!m_ReadUnixSeconds) {
			throw std::runtime_error("Emote wind random requires the actual Unix-seconds service");
		}
	}
	float EmoteWindRandom::NextUnitFloat() {
		if (m_Thread != std::this_thread::get_id() || m_Seeding) {
			throw std::runtime_error("Emote wind random cannot reenter or cross its application owner thread");
		}
		if (!m_State) {
			m_Seeding = true;
			std::int64_t seconds = 0;
			try {
				seconds = m_ReadUnixSeconds();
				m_Seeding = false;
			} catch (...) {
				m_Seeding = false;
				throw;
			}
			// Native582368 preserves negative seconds; only its positive upper bound maps to -1.
			if (seconds > std::int64_t{0x793582aff}) {
				seconds = -1;
			}
			m_State = std::array<std::uint32_t, 4>{0x075bcd15, 0x159a55e5, 0x1f123bb5, static_cast<std::uint32_t>(seconds)};
		}
		auto& words = *m_State;
		const auto temporary = words[0] ^ (words[0] << 11);
		const auto previous = words[3];
		words[0] = words[1];
		words[1] = words[2];
		words[2] = previous;
		words[3] = (((previous >> 11) ^ temporary) >> 8) ^ previous ^ temporary;
		return std::bit_cast<float>((words[3] & 0x007fffff) | 0x3f800000) - 1.0f;
	}
	std::optional<std::array<std::uint32_t, 4>> EmoteWindRandom::State() const {
		if (m_Thread != std::this_thread::get_id() || m_Seeding) {
			throw std::runtime_error("Emote wind random snapshot requires its idle application owner thread");
		}
		return m_State;
	}

	EmoteWindOwner::EmoteWindOwner(float metadata_scale, EmoteWindRandom* shared_random) : m_MetadataScale(metadata_scale), m_Random(shared_random) {
		if (!std::isfinite(metadata_scale) || !(metadata_scale > 0) || !std::isfinite(1.0f / metadata_scale)) {
			throw std::runtime_error("Emote wind requires finite positive actual metadata scale");
		}
	}
	void EmoteWindOwner::RequireIdle() const {
		if (m_Thread != std::this_thread::get_id() || m_Operating) {
			throw std::runtime_error("Emote wind requires its idle containing owner thread");
		}
	}
	void EmoteWindOwner::Start(EmoteWindParameters parameters) {
		RequireIdle();
		if (!std::isfinite(parameters.Start) || !std::isfinite(parameters.Goal) || !std::isfinite(parameters.Speed) || !std::isfinite(parameters.Minimum) || !std::isfinite(parameters.Maximum)) {
			throw std::runtime_error("Emote wind requires finite native control parameters");
		}
		if (parameters.Speed < 0) {
			std::swap(parameters.Start, parameters.Goal);
			parameters.Speed = -parameters.Speed;
		}
		if (parameters.Start == parameters.Goal || parameters.Speed == 0 || (parameters.Minimum == 0 && parameters.Maximum == 0)) {
			// Native2d162f deletes only the controller, preserving all five authored property words.
			m_Control.reset();
			return;
		}
		if (m_Random == nullptr) {
			throw std::runtime_error("Emote wind requires the actual shared lazy random service");
		}
		const float start = parameters.Start / m_MetadataScale;
		const float goal = parameters.Goal / m_MetadataScale;
		const float speed = parameters.Speed / m_MetadataScale;
		if (!std::isfinite(start) || !std::isfinite(goal) || !std::isfinite(goal - start) || !std::isfinite(speed) || !std::isfinite(parameters.Minimum - parameters.Maximum)) {
			throw std::runtime_error("Emote wind control exceeds finite calibrated arithmetic");
		}
		if (!m_Control || m_Parameters.Start != parameters.Start || m_Parameters.Goal != parameters.Goal) {
			auto next = std::make_unique<EmoteWindControlState>();
			next->Start = start;
			next->Goal = goal;
			m_Control = std::move(next);
		}
		m_Parameters = parameters;
		m_Control->Minimum = parameters.Minimum;
		m_Control->Maximum = parameters.Maximum;
		m_Control->SignedVelocity = (m_Control->Goal - m_Control->Start < 0 ? -1.0f : 1.0f) * speed;
		m_Control->Enabled = true;
		m_Control->BirthClock = 0;
	}
	void EmoteWindOwner::Stop() {
		Start({});
	}
	void EmoteWindOwner::AssignState(const EmoteWindOwner& source) {
		RequireIdle();
		source.RequireIdle();
		if (this == &source) {
			return;
		}
		const auto preceding = m_Parameters;
		m_Parameters = source.m_Parameters;
		try {
			// This order is significant: native 2BC982 copies 260..270 before 2BCA48 calls Start.
			Start(m_Parameters);
		} catch (...) {
			m_Parameters = preceding;
			throw;
		}
	}
	void EmoteWindOwner::Advance(float elapsed_ticks) {
		RequireIdle();
		if (!std::isfinite(elapsed_ticks) || elapsed_ticks < 0) {
			throw std::runtime_error("Emote wind requires finite nonnegative elapsed ticks");
		}
		if (!m_Control || !m_Control->Enabled) {
			return;
		}
		auto& control = *m_Control;
		const float clock = control.BirthClock + elapsed_ticks;
		const float movement = elapsed_ticks * control.SignedVelocity;
		if (!std::isfinite(clock) || clock >= kMaximumWindBirthTrials || !std::isfinite(movement)) {
			throw std::runtime_error("Emote wind advance exceeds bounded finite birth work");
		}
		for (const auto& band : control.Bands) {
			if (band.Active && !std::isfinite(band.Position + movement)) {
				throw std::runtime_error("Emote wind advance exceeds finite active band positions");
			}
		}
		m_Operating = true;
		try {
			control.BirthClock = clock;
			while (control.BirthClock >= 0) {
				control.BirthClock -= 1;
				if (m_Random->NextUnitFloat() < 0.0625f) {
					for (auto& band : control.Bands) {
						if (!band.Active) {
							band.Active = true;
							band.Position = control.Start;
							const float difference = control.Minimum - control.Maximum;
							const float offset = difference * m_Random->NextUnitFloat();
							band.Strength = offset + control.Maximum;
							break;
						}
					}
				}
			}
			for (auto& band : control.Bands) {
				if (band.Active) {
					band.Position = movement + band.Position;
					if ((control.SignedVelocity > 0 && band.Position > control.Goal) || (control.SignedVelocity < 0 && band.Position < control.Goal)) {
						band.Active = false;
					}
				}
			}
			m_Operating = false;
		} catch (...) {
			m_Operating = false;
			throw;
		}
	}
	float EmoteWindOwner::Sample(float position) const {
		RequireIdle();
		if (!std::isfinite(position)) {
			throw std::runtime_error("Emote wind sampling requires finite positions");
		}
		if (!m_Control) {
			return 0;
		}
		for (const auto& band : m_Control->Bands) {
			if (band.Active) {
				const float half_strength = band.Strength * 0.5f;
				const float half_width = half_strength * 4.0f;
				if (position > band.Position - half_width && band.Position + half_width > position) {
					return (m_Control->SignedVelocity < 0 ? -1.0f : 1.0f) * band.Strength;
				}
			}
		}
		return 0;
	}
	EmoteWindSnapshot EmoteWindOwner::Snapshot() const {
		RequireIdle();
		EmoteWindSnapshot snapshot;
		snapshot.Parameters = m_Parameters;
		if (m_Control) {
			snapshot.Control = *m_Control;
		}
		return snapshot;
	}
} // namespace sgre::scene
