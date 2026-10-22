#include "sgre/scene/motion_numeric_storage.hpp"

#include "sgre/scene/motion_layer_composition.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sgre::scene {
	struct MotionNumericCell {
		const MotionComposedLayer* Current = nullptr;
		MotionLayerNumbers Retained;
	};

	struct MotionNumericStorageState {
		std::vector<MotionNumericCell> Cells{1};
		std::size_t ActiveCount = 1;
		std::uint64_t Allocation = 1;
	};

	static constexpr std::size_t kMaximumNumericRows = 65537;

	static MotionLayerNumbers ReadCell(const MotionNumericCell& cell) noexcept {
		if (cell.Current == nullptr) {
			return cell.Retained;
		}
		const auto& layer = *cell.Current;
		const auto& attributes = layer.Hierarchy.Attributes;
		MotionLayerNumbers result;
		result.Coordinate = attributes.Coordinate;
		result.Angle = attributes.Angle;
		result.ZoomX = attributes.ZoomX;
		result.ZoomY = attributes.ZoomY;
		result.ShearX = attributes.ShearX;
		result.ShearY = attributes.ShearY;
		result.Opacity = attributes.Opacity;
		result.FlipX = attributes.FlipX;
		result.FlipY = attributes.FlipY;
		result.Visible = layer.Active;
		result.BranchVisible = layer.Visible;

		return result;
	}

	MotionLayerNumbers MotionNumericBorrow::Read() const {
		const auto state = m_Storage.lock();
		if (!state || state->Allocation != m_Allocation || m_Ordinal >= state->Cells.size()) {
			throw std::runtime_error("motion numeric getter allocation has retired");
		}

		return ReadCell(state->Cells[m_Ordinal]);
	}

	MotionNumericStorage::MotionNumericStorage()
	    : m_State(std::make_shared<MotionNumericStorageState>()) {
	}

	MotionNumericStorage::~MotionNumericStorage() = default;

	bool MotionNumericStorage::Owns(const MotionNumericBorrow& reference) const {
		return reference.m_Storage.lock() == m_State && reference.m_Allocation == m_State->Allocation && reference.m_Ordinal != 0 && reference.m_Ordinal < m_State->ActiveCount;
	}

	MotionNumericBorrow MotionNumericStorage::Borrow(std::uint32_t native_ordinal) const {
		if (native_ordinal == 0 || native_ordinal >= m_State->ActiveCount) {
			throw std::runtime_error("motion numeric getter requires its actual selected nonroot row");
		}
		MotionNumericBorrow result;
		result.m_Storage = m_State;
		result.m_Allocation = m_State->Allocation;
		result.m_Ordinal = native_ordinal;

		return result;
	}

	void MotionNumericStorage::Install(MotionLayerComposition& composition) {
		if (composition.m_NumericStorage != nullptr || m_State->ActiveCount != 1 || composition.m_Layers.size() >= kMaximumNumericRows) {
			throw std::runtime_error("motion numeric installation requires its retired physical player storage and bounded selected rows");
		}
		const auto count = composition.m_Layers.size() + 1;
		auto capacity = m_State->Cells.size();
		for (auto required = capacity + 1; required <= count; ++required) {
			// Native4ff640 appends with max(size+1, capacity+capacity/2); Clear retains that allocation.
			if (required > capacity) {
				capacity = std::max(required, capacity + capacity / 2);
			}
		}
		if (capacity != m_State->Cells.size()) {
			if (m_State->Allocation == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("motion numeric allocation serial exceeds its bounded identity");
			}
			m_State->Cells.resize(capacity);
			++m_State->Allocation;
		}
		for (std::size_t row = 1; row != count; ++row) {
			m_State->Cells[row].Current = &composition.m_Layers[row - 1];
		}
		m_State->ActiveCount = count;
		composition.m_NumericStorage = this;
	}

	void MotionNumericStorage::Retire(MotionLayerComposition& composition) noexcept {
		for (std::size_t row = 1; row < m_State->ActiveCount; ++row) {
			auto& cell = m_State->Cells[row];
			cell.Retained = ReadCell(cell);
			cell.Current = nullptr;
		}
		m_State->ActiveCount = 1;
		composition.m_NumericStorage = nullptr;
	}

	void MotionNumericStorage::CopyForClone(const MotionNumericStorage& source) {
		if (m_State->ActiveCount != 1 || m_State->Cells.size() != 1) {
			throw std::runtime_error("motion numeric clone requires fresh destination storage");
		}
		// Native510c00/4fdc70 assigns the live size, rather than copying spare source capacity.
		m_State->Cells.resize(source.m_State->ActiveCount);
		for (std::size_t row = 1; row < source.m_State->ActiveCount; ++row) {
			m_State->Cells[row].Retained = ReadCell(source.m_State->Cells[row]);
		}
	}

} // namespace sgre::scene
