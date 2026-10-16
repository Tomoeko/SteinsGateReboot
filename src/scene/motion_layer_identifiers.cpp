#include "sgre/scene/motion_layer_identifiers.hpp"

#include <limits>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumMotionLayerIdentifiers = 1000000;

	std::uint32_t MotionLayerIdentifier::Value() const noexcept {
		return m_Value;
	}

	MotionLayerIdentifierPool::MotionLayerIdentifierPool(std::size_t maximum_identifiers) {
		if (maximum_identifiers == 0 || maximum_identifiers > kMaximumMotionLayerIdentifiers) {
			throw std::runtime_error("motion layer identifier capacity must be bounded and positive");
		}

		m_Entries.resize(maximum_identifiers + 1);
		m_Retired.reserve(maximum_identifiers);
	}

	MotionLayerIdentifier MotionLayerIdentifierPool::Acquire() {
		const std::lock_guard lock(m_Mutex);
		const auto value = m_Retired.empty() ? m_Next : m_Retired.back();
		if (value >= m_Entries.size()) {
			throw std::runtime_error("motion layer identifiers exceed their startup capacity");
		}

		auto& entry = m_Entries[value];
		if (entry.Live || entry.Generation == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion layer identifier has no valid fresh ownership generation");
		}

		if (m_Retired.empty()) {
			++m_Next;
		} else {
			m_Retired.pop_back();
		}

		entry.Live = true;
		++entry.Generation;
		MotionLayerIdentifier identifier;
		identifier.m_Owner = this;
		identifier.m_Value = value;
		identifier.m_Generation = entry.Generation;

		return identifier;
	}

	bool MotionLayerIdentifierPool::ContainsUnlocked(MotionLayerIdentifier identifier) const {
		return identifier.m_Owner == this && identifier.m_Value != 0 && identifier.m_Value < m_Next && m_Entries[identifier.m_Value].Live && m_Entries[identifier.m_Value].Generation == identifier.m_Generation;
	}

	void MotionLayerIdentifierPool::Retire(MotionLayerIdentifier identifier) {
		const std::lock_guard lock(m_Mutex);
		if (!ContainsUnlocked(identifier)) {
			throw std::runtime_error("motion layer identifier is expired or belongs to another manager");
		}

		m_Retired.push_back(identifier.m_Value);
		m_Entries[identifier.m_Value].Live = false;
	}

	bool MotionLayerIdentifierPool::Contains(MotionLayerIdentifier identifier) const {
		const std::lock_guard lock(m_Mutex);

		return ContainsUnlocked(identifier);
	}

	std::size_t MotionLayerIdentifierPool::LiveCount() const {
		const std::lock_guard lock(m_Mutex);

		return static_cast<std::size_t>(m_Next - 1) - m_Retired.size();
	}

	std::size_t MotionLayerIdentifierPool::RetiredCount() const {
		const std::lock_guard lock(m_Mutex);

		return m_Retired.size();
	}

	std::uint32_t MotionLayerIdentifierPool::NextIdentifier() const {
		const std::lock_guard lock(m_Mutex);

		return m_Next;
	}

} // namespace sgre::scene
