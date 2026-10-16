#include "sgre/scene/motion_copied_region_owner.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static void RequireCopiedRectangle(const MotionBounds& rectangle) {
		const std::array<float, 4> values{rectangle.MinimumX, rectangle.MinimumY, rectangle.MaximumX, rectangle.MaximumY};
		for (const auto value : values) {
			if (!std::isfinite(value) || value < 0 || value != std::trunc(value) || value >= 2147483648.0f) {
				throw std::runtime_error("motion copied region requires nonnegative bounded integer coordinates");
			}
		}
		if (rectangle.MinimumX >= rectangle.MaximumX || rectangle.MinimumY >= rectangle.MaximumY) {
			throw std::runtime_error("motion copied region rectangle is empty");
		}
	}

	MotionCopiedRegionOwner::MotionCopiedRegionOwner(MutableTextureBackend& textures, MotionCopiedTargetBackend& targets, MotionRegionCopyBackend& copies, const MutableTextureLimits& limits) : m_Target(textures, targets, limits), m_Copies(copies) {
	}

	MotionCopiedRegionOwner::~MotionCopiedRegionOwner() {
		Close();
	}

	bool MotionCopiedRegionOwner::Apply(const MotionCopyRegionPlan& plan, std::uint32_t source_format) {
		if (m_Target.State().Closed) {
			throw std::runtime_error("motion copied region owner has closed");
		}
		if (plan.Action == MotionCopyRegionAction::Disable) {
			m_Selected = false;
			return false;
		}
		if (plan.Action == MotionCopyRegionAction::Preserve) {
			return false;
		}
		if (plan.Action != MotionCopyRegionAction::Copy) {
			throw std::runtime_error("motion copied region action is invalid");
		}

		RequireCopiedRectangle(plan.Rectangle);
		try {
			if (m_Target.Ensure(plan.Rectangle, source_format)) {
				m_Selected = false;
			}
		} catch (...) {
			if (!m_Target.State().HasTexture) {
				m_Selected = false;
			}
			throw;
		}
		auto* texture = m_Target.Texture();
		if (texture == nullptr) {
			throw std::runtime_error("motion copied region has no texture after render-target view creation");
		}

		m_Copies.CopyFromCurrentTarget(texture->Resource(), plan.Rectangle);
		texture->SetFiltering(0, 0);
		texture->SetAddress(0, 0);
		m_Selected = true;

		const auto size = m_Target.State().Size;
		const auto width = static_cast<float>(size.Width);
		const auto height = static_cast<float>(size.Height);
		m_UvAffine = {1.0f / width, 1.0f / height, (-plan.Rectangle.MinimumX + 0.0f) / width, (-plan.Rectangle.MinimumY + 0.0f) / height};
		return true;
	}

	void MotionCopiedRegionOwner::Close() noexcept {
		m_Selected = false;
		m_Target.Close();
	}

	MotionCopiedRegionState MotionCopiedRegionOwner::State() const noexcept {
		return {m_Target.State(), m_UvAffine, m_Selected};
	}

	MutableTextureOwner* MotionCopiedRegionOwner::SelectedTexture() noexcept {
		return m_Selected ? m_Target.Texture() : nullptr;
	}

	const MutableTextureOwner* MotionCopiedRegionOwner::TargetTexture() const noexcept {
		return m_Target.Texture();
	}
} // namespace sgre::scene
