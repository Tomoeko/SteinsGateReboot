#include "sgre/scene/emote_selector_control.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	struct EmoteSelectorApplyScope {
		bool& Active;

		explicit EmoteSelectorApplyScope(bool& active) : Active(active) {
			if (active) {
				throw std::runtime_error("Emote selector cannot reenter its active variable IO");
			}
			Active = true;
		}

		~EmoteSelectorApplyScope() noexcept {
			Active = false;
		}
	};

	static void Validate(const resource::EmoteSelectorDefinition& definition) {
		const resource::EmoteSelectorLimits limits;
		if (definition.Options.size() > limits.MaxOptions || definition.Label.size() > limits.MaxStringBytes || definition.Label.find('\0') != std::string::npos) {
			throw std::runtime_error("Emote selector options exceed the control bound");
		}
		std::size_t strings = definition.Label.size();
		std::size_t ranges = 0;
		for (const auto& option : definition.Options) {
			if (!std::isfinite(option.OffValue) || !std::isfinite(option.OnValue) || option.Variable.find('\0') != std::string::npos || option.Variable.size() > limits.MaxStringBytes - strings || option.Ranges.size() > limits.MaxRanges - ranges) {
				throw std::runtime_error("Emote selector definition exceeds its finite bounded domain");
			}
			strings += option.Variable.size();
			ranges += option.Ranges.size();
		}
	}

	EmoteSelectorControl::EmoteSelectorControl(resource::EmoteSelectorDefinition definition, EmoteVariableIo& variables) : m_Definition(std::move(definition)), m_Variables(variables) {
		Validate(m_Definition);
		Apply(0, 0, 0);
	}

	void EmoteSelectorControl::Apply(std::int32_t selection, float duration, float easing) {
		if (!std::isfinite(duration) || duration < 0 || !std::isfinite(easing)) {
			throw std::runtime_error("Emote selector application requires finite nonnegative duration and finite easing");
		}
		const EmoteSelectorApplyScope scope(m_Applying);
		m_Selection = selection;
		for (const auto& option : m_Definition.Options) {
			if (option.Variable.empty()) {
				continue;
			}
			const bool selected = std::any_of(option.Ranges.begin(), option.Ranges.end(), [selection](const auto& range) {
				return range.Lower <= selection && selection <= range.Upper;
			});
			static_cast<void>(m_Variables.Read(option.Variable));
			m_Variables.Set(option.Variable, selected ? option.OnValue : option.OffValue, option.Immediate ? 0 : duration, easing);
		}
	}

	static constexpr std::size_t kMaximumSelectorTransitions = 4096;

	void EmoteSelectorControl::RequireCanAssignState(const EmoteSelectorControl& source) const {
		if (m_Applying || m_Updating || source.m_Applying || source.m_Updating || source.m_Pending.size() > kMaximumSelectorTransitions) {
			throw std::runtime_error("Emote selector assignment requires two idle bounded actual controls");
		}
	}

	void EmoteSelectorControl::AssignState(const EmoteSelectorControl& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		auto pending = source.m_Pending;
		m_Selection = source.m_Selection;
		m_ReciprocalDuration = source.m_ReciprocalDuration;
		m_Progress = source.m_Progress;
		m_Transitioning = source.m_Transitioning;
		m_Pending = std::move(pending);
	}

	void EmoteSelectorControl::Set(float selection, float duration, float easing, bool append) {
		if (m_Applying || m_Updating || !std::isfinite(selection) || selection < -2147483648.0f || selection >= 2147483648.0f || !std::isfinite(duration) || !std::isfinite(easing)) {
			throw std::runtime_error("Emote selector setter requires finite native values and idle variable IO");
		}
		if (!(duration > 0)) {
			m_Pending.clear();
			m_Transitioning = false;
			Apply(static_cast<std::int32_t>(selection), 0, 0);
			return;
		}
		if (!std::isfinite(1.0f / duration) || (append && m_Pending.size() == kMaximumSelectorTransitions)) {
			throw std::runtime_error("Emote selector transition exceeds its finite reciprocal or bounded queue");
		}
		if (!append) {
			m_Pending.clear();
			m_Transitioning = false;
		}
		m_Pending.push_back({selection, duration, easing});
	}

	void EmoteSelectorControl::Update(float elapsed_ticks) {
		if (m_Applying || m_Updating || !std::isfinite(elapsed_ticks) || elapsed_ticks < 0 || elapsed_ticks > 1000000.0f) {
			throw std::runtime_error("Emote selector update requires an idle bounded nonnegative native delta");
		}
		m_Updating = true;
		try {
			while (true) {
				if (!m_Transitioning) {
					if (m_Pending.empty()) {
						break;
					}
					const auto pending = m_Pending.front();
					m_Pending.pop_front();
					Apply(static_cast<std::int32_t>(pending.Selection), pending.Duration, pending.Easing);
					m_Transitioning = true;
					m_ReciprocalDuration = 1.0f / pending.Duration;
					m_Progress = 0;
				}
				m_Progress = elapsed_ticks * m_ReciprocalDuration + m_Progress;
				if (!(m_Progress - 1.0f >= std::bit_cast<float>(std::uint32_t{0xb8d1b717}))) {
					break;
				}
				m_Progress = 1;
				m_Transitioning = false;
			}
		} catch (...) {
			m_Updating = false;
			throw;
		}
		m_Updating = false;
	}

	void EmoteSelectorControl::FinishTransitions() {
		if (m_Applying || m_Updating) {
			throw std::runtime_error("Emote selector completion requires idle variable IO");
		}
		if (!m_Pending.empty()) {
			m_Transitioning = false;
			Apply(static_cast<std::int32_t>(m_Pending.back().Selection), 0, 0);
			m_Pending.clear();
		} else if (m_Transitioning) {
			m_Transitioning = false;
			Apply(m_Selection, 0, 0);
		}
	}

	EmoteSelectorTransitionState EmoteSelectorControl::TransitionState() const noexcept {
		return {m_Pending.size(), m_ReciprocalDuration, m_Progress, m_Transitioning};
	}

	std::int32_t EmoteSelectorControl::Selection() const noexcept {
		return m_Selection;
	}

	const resource::EmoteSelectorDefinition& EmoteSelectorControl::Definition() const noexcept {
		return m_Definition;
	}

} // namespace sgre::scene
