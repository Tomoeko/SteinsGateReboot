#include "sgre/scene/raw_texture_task.hpp"

#include "sgre/scene/raw_image.hpp"
#include "sgre/scene/raw_texture_color.hpp"
#include "sgre/scene/raw_texture_restore.hpp"

#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	static std::uint32_t RawTextureFormat(std::string_view color_mode) {
		color_mode = color_mode.substr(0, color_mode.find('\0'));
		if (color_mode == "8888" || color_mode == "8888_BGRA") {
			return 87;
		}
		if (color_mode == "8888_BGRX") {
			return 88;
		}
		if (color_mode == "8888_RGBA") {
			return 28;
		}

		throw std::runtime_error("RawTex color mode is outside the recovered four-format domain");
	}

	static constexpr std::uint32_t kMaxRawTextureExtent = 16384;

	static std::uint32_t RawTextureExtent(float extent) {
		if (!std::isfinite(extent) || extent < 1 || extent > static_cast<float>(kMaxRawTextureExtent)) {
			throw std::runtime_error("RawTex requires bounded positive texture extents");
		}

		return static_cast<std::uint32_t>(extent);
	}

	RawTextureTask::RawTextureTask(MutableTextureBackend& backend, resource::MotionPoint logical_extent, std::string_view color_mode, std::size_t max_tasks_per_group, SerialTaskSet::Completed completed) : m_Serial(max_tasks_per_group, std::move(completed)), m_Parallel(max_tasks_per_group) {
		MutableTextureSpec spec;
		spec.Storage = MutableTextureStorage::CpuShadow;
		spec.Description.Width = RawTextureExtent(logical_extent.X);
		spec.Description.Height = RawTextureExtent(logical_extent.Y);
		spec.Description.OriginalFormat = RawTextureFormat(color_mode);
		m_State.LogicalExtent = logical_extent;
		m_Serial.SetRetireWhenEmpty(false);
		m_Parallel.SetRetireWhenEmpty(false);
		m_Texture = std::make_unique<MutableTextureOwner>(backend, spec);
		m_Texture->SetFiltering(0, 0);
	}

	RawTextureTask::~RawTextureTask() {
		try {
			Close();
		} catch (...) {
			// Explicit Close reports callback failures after all actual ownership has retired.
		}
	}

	void RawTextureTask::RequireOpen() const {
		if (m_State.Closed) {
			throw std::runtime_error("RawTex task has retired its texture and process groups");
		}
	}

	RawTextureTaskState RawTextureTask::State() const noexcept {
		return m_State;
	}

	MutableTextureOwner& RawTextureTask::Texture() {
		RequireOpen();

		return *m_Texture;
	}

	SerialTaskSet& RawTextureTask::SerialTasks() {
		RequireOpen();

		return m_Serial;
	}

	ParallelTaskSet& RawTextureTask::ParallelTasks() {
		RequireOpen();

		return m_Parallel;
	}

	void RawTextureTask::SetCoordinate(resource::MotionPoint coordinate) {
		RequireOpen();
		m_State.Coordinate = coordinate;
	}

	void RawTextureTask::SetColor(const std::array<float, 3>& rgb) {
		RequireOpen();
		m_State.PackedRgb = PackRawTextureColor(rgb);
	}

	void RawTextureTask::SetOpacity(std::uint32_t opacity) {
		RequireOpen();
		m_State.Opacity = opacity;
	}

	void RawTextureTask::SetFiltering(std::uint32_t minimum, std::uint32_t maximum) {
		Texture().SetFiltering(minimum, maximum);
	}

	void RawTextureTask::SetStatus(TaskStatus status) {
		RequireOpen();
		m_State.Task.Status = status;
	}

	void RawTextureTask::SetDrawPaused(bool paused) {
		RequireOpen();
		m_State.Task.DrawPaused = paused;
	}

	void RawTextureTask::RestoreImage(const RawImage& image) {
		RequireOpen();
		const auto description = m_Texture->State().Description;
		if (image.Width() != description.Width || image.Height() != description.Height) {
			throw std::runtime_error("RawTex image size mismatch");
		}
		if (static_cast<float>(image.Width()) != m_State.LogicalExtent.X || static_cast<float>(image.Height()) != m_State.LogicalExtent.Y) {
			// Native image-copy helper leaves fractional logical extents untouched.
			return;
		}

		const auto mapping = m_Texture->Map();
		try {
			const auto pitch = m_Texture->RowPitch();
			RestoreRawTextureBgra({mapping.Bytes, image.Width(), image.Height(), pitch}, image.BgraPixels(), image.Width(), image.Height());
		} catch (...) {
			m_Texture->Unmap();

			throw;
		}
		m_Texture->Unmap();
	}

	void RawTextureTask::RestoreRgb(std::span<const std::uint8_t> rgb) {
		RequireOpen();
		const auto description = m_Texture->State().Description;
		if (rgb.size() != std::size_t{description.Width} * description.Height * 3) {
			throw std::runtime_error("RawTex RGB byte count differs from its texture dimensions");
		}

		const std::vector<std::uint8_t> snapshot(rgb.begin(), rgb.end());
		const auto mapping = m_Texture->Map();
		try {
			const auto pitch = m_Texture->RowPitch();
			RestoreRawTextureRgb({mapping.Bytes, description.Width, description.Height, pitch}, snapshot);
		} catch (...) {
			m_Texture->Unmap();

			throw;
		}
		m_Texture->Unmap();
	}

	void RawTextureTask::Update() {
		RequireOpen();
		if (m_State.Task.Status != TaskStatus::Active) {
			return;
		}

		if (m_Updating) {
			throw std::runtime_error("RawTex cannot reenter its process update");
		}

		m_Updating = true;
		try {
			if (m_Serial.Status() == TaskStatus::Active) {
				m_Serial.Update();
			}
			if (m_Parallel.Status() == TaskStatus::Active) {
				m_Parallel.Update();
			}
		} catch (...) {
			m_Updating = false;

			throw;
		}
		m_Updating = false;
	}

	RawTextureDrawPacket RawTextureTask::PrepareDrawPacket() const {
		RequireOpen();

		return PrepareRawTextureDrawPacket(m_State.Coordinate, m_State.LogicalExtent, PrepareRawTextureDrawColor(m_State.PackedRgb, m_State.Opacity));
	}

	bool RawTextureTask::CanClose() const noexcept {
		return m_State.Closed || (!m_Updating && m_Serial.CanClear() && m_Parallel.CanClear());
	}

	void RawTextureTask::Close() {
		if (m_State.Closed) {
			return;
		}

		if (!CanClose()) {
			throw std::runtime_error("RawTex cannot close during its process update or task callback");
		}

		m_State.Closed = true;
		m_Texture.reset();
		std::exception_ptr failure;
		try {
			m_Parallel.Clear();
		} catch (...) {
			failure = std::current_exception();
		}
		try {
			m_Serial.Clear();
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}

		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::scene
