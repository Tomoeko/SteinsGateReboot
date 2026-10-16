#include "sgre/scene/captured_task.hpp"

#include "sgre/scene/image_assets.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	RawTextureDrawPacket PrepareCapturedDrawPacket(const CapturedTaskState& actual_state) {
		if (!std::isfinite(actual_state.Opacity) || actual_state.Opacity < 0 || actual_state.Opacity > 255) {
			throw std::runtime_error("Captured packet requires its actual finite clamped opacity");
		}
		auto packet = PrepareRawTextureDrawPacket(actual_state.Coordinate, actual_state.FixedSize, (static_cast<std::uint32_t>(actual_state.Opacity) << 24) | 0x00ffffffu);
		packet.TextureAttributes = actual_state.Opacity == 255.0f ? 4u : 0u;

		return packet;
	}

	CapturedTask::CapturedTask(const std::shared_ptr<CaptureTask>& actual_source) : m_Source(actual_source) {
		if (!actual_source || actual_source->State().Closed) {
			throw std::runtime_error("Captured requires its actual live Capture physical source");
		}
		const auto source = actual_source->State();
		const auto& bounds = source.PhysicalBounds;
		m_State.FixedSize = {bounds.MaximumX - bounds.MinimumX, bounds.MaximumY - bounds.MinimumY};
		m_State.Ready = source.Captured;
		actual_source->AddConsumer(*this);
	}

	CapturedTask::~CapturedTask() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void CapturedTask::RequireIdle() const {
		if (m_Thread != std::this_thread::get_id() || m_State.Closed || m_Drawing) {
			throw std::runtime_error("Captured requires its live idle task on the actual frame thread");
		}
	}

	CapturedTaskState CapturedTask::State() const noexcept {
		return m_State;
	}

	void CapturedTask::SetOpacity(float opacity) {
		RequireIdle();
		if (!std::isfinite(opacity)) {
			throw std::runtime_error("Captured opacity requires a finite actual scalar");
		}
		// Native4fb950 clamps before4fb398 truncates to the low-byte draw alpha.
		m_State.Opacity = std::max(std::min(opacity, 255.0f), 0.0f);
	}

	void CapturedTask::SetOpacityMap(const LoadedImageAsset& actual_image, MotionTextureWrapper::Borrowed actual_source, std::int32_t width) {
		RequireIdle();
		if (!actual_image.Valid || actual_image.CacheOwner == nullptr || actual_image.Model.Tiles.empty() || actual_image.Model.Width == 0 || actual_image.Model.Height == 0 || !actual_source.IsLive()) {
			throw std::runtime_error("Captured opacity map requires its actual live Image and first texture source");
		}
		const auto& first = actual_image.Model.Tiles.front().Texture;
		const auto cached = actual_image.CacheOwner->Get(actual_source.Texture());
		if (!first.SerializedValueOffset || cached.Identity.DocumentInstance != actual_image.DocumentIdentity || cached.Identity.ValueOffset != *first.SerializedValueOffset || cached.Description.Width != first.Width || cached.Description.Height != first.Height || first.Width == 0 || first.Height == 0 || first.Width > 0x7fffffffu || first.Height > 0x7fffffffu) {
			throw std::runtime_error("Captured opacity map requires the exact first Image source and native signed texture dimensions");
		}
		const resource::MotionPoint ratio{static_cast<float>(actual_image.Model.Width) / static_cast<float>(first.Width), static_cast<float>(actual_image.Model.Height) / static_cast<float>(first.Height)};
		// Native4fb9b7/9c2 use CMOVB/CMOVA: the converted script int is an unsigned physical word.
		m_OpacityMap.Width = std::clamp(std::bit_cast<std::uint32_t>(width), 1u, 765u);
		m_OpacityMap.ImageRatio = ratio;
		m_MapSource = std::move(actual_source);
	}

	void CapturedTask::ResetOpacityMap() {
		RequireIdle();
		m_MapSource.reset();
	}

	void CapturedTask::SetOpacityMapSmoothing(bool smoothing) {
		RequireIdle();
		// Native28f110's named smoothing flag selects the map's two address indices, not its linear filter.
		m_OpacityMap.AddressU = smoothing ? 1u : 0u;
		m_OpacityMap.AddressV = smoothing ? 1u : 0u;
	}

	void CapturedTask::SetOpacityMapRepeat(resource::MotionPoint repeat) {
		RequireIdle();
		if (!std::isfinite(repeat.X) || !std::isfinite(repeat.Y)) {
			throw std::runtime_error("Captured opacity map repeat requires finite native float operands");
		}
		m_OpacityMap.Repeat = repeat;
	}

	CapturedOpacityMapState CapturedTask::OpacityMapState() const noexcept {
		return m_OpacityMap;
	}

	void CapturedTask::Draw(const DrawTexture& actual_drawing, const DrawOpacityMap& actual_map_drawing) {
		RequireIdle();
		if (m_State.SourceRetired || !m_State.Ready) {
			return;
		}
		const auto source = m_Source.lock();
		if (!source || source->State().Closed) {
			throw std::runtime_error("Captured source lifetime ended without its actual retirement notification");
		}
		if ((!m_MapSource && !actual_drawing) || (m_MapSource && !actual_map_drawing)) {
			throw std::runtime_error("Captured requires its actual selected ordinary or opacity-map frame recipient");
		}
		if (m_MapSource && !m_MapSource->IsLive()) {
			throw std::runtime_error("Captured opacity map Image source retired before its borrowed draw");
		}
		m_Drawing = true;
		try {
			if (m_MapSource) {
				const auto packet = PrepareCapturedOpacityMapDrawPacket(m_State, m_OpacityMap);
				m_MapSource->SetFiltering(1, 1);
				m_MapSource->SetAddress(m_OpacityMap.AddressU, m_OpacityMap.AddressV);
				actual_map_drawing(source->Texture(), *m_MapSource, packet);
			} else {
				const auto packet = PrepareCapturedDrawPacket(m_State);
				actual_drawing(source->Texture(), packet);
			}
		} catch (...) {
			m_Drawing = false;
			throw;
		}
		m_Drawing = false;
	}

	bool CapturedTask::CanClose() const noexcept {
		const auto source = m_Source.lock();

		return m_Thread == std::this_thread::get_id() && !m_Drawing && (!source || !source->State().FramePending);
	}

	void CapturedTask::Close() {
		if (m_State.Closed) {
			return;
		}
		RequireIdle();
		if (!CanClose()) {
			throw std::runtime_error("Captured retirement requires its actual source frame End or Abort");
		}
		if (!m_State.SourceRetired) {
			const auto source = m_Source.lock();
			if (!source) {
				throw std::runtime_error("Captured source was destroyed without clearing its actual backlink");
			}
			source->RemoveConsumer(*this);
		}
		m_Source.reset();
		m_MapSource.reset();
		m_State.Ready = false;
		m_State.Closed = true;
	}

	void CapturedTask::CaptureResized(resource::MotionPoint physical_size) noexcept {
		m_State.FixedSize = physical_size;
		m_State.Ready = true;
	}

	void CapturedTask::CaptureInvalidated() noexcept {
		m_State.Ready = false;
	}

	void CapturedTask::CaptureCompleted() noexcept {
		m_State.Ready = true;
	}

	void CapturedTask::CaptureRetired() noexcept {
		m_Source.reset();
		m_State.Ready = false;
		m_State.SourceRetired = true;
	}
} // namespace sgre::scene
