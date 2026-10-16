#include "sgre/scene/capture_task.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::scene {
	static bool SameCaptureBounds(const MotionBounds& first, const MotionBounds& second) noexcept {
		return first.MinimumX == second.MinimumX && first.MinimumY == second.MinimumY && first.MaximumX == second.MaximumX && first.MaximumY == second.MaximumY;
	}

	MotionBounds PrepareCapturePhysicalBounds(const MotionBounds& logical, const CaptureFrameContext& frame) {
		if (frame.NativeTargetType == 2 || frame.NativeTargetType == 3) {
			throw std::runtime_error("Capture split-target source-region selection requires its native4c2850 receiving phase");
		}
		auto physical = TransformMotionScissorBounds(logical, frame.View, frame.Model, frame.Viewport);
		physical.MinimumX = std::max(0.0f, physical.MinimumX);
		physical.MinimumY = std::max(0.0f, physical.MinimumY);
		physical.MaximumX = std::min(physical.MaximumX, static_cast<float>(frame.Viewport.Width));
		physical.MaximumY = std::min(physical.MaximumY, static_cast<float>(frame.Viewport.Height));
		const auto rounded = PrepareMotionScissorUpdate(physical, {}).Rounded;
		if (!(rounded.MaximumX > rounded.MinimumX) || !(rounded.MaximumY > rounded.MinimumY)) {
			throw std::runtime_error("Capture actual projected/clipped rectangle requires a positive physical copy extent");
		}
		return rounded;
	}

	class CaptureTask::Implementation {
	public:
		Implementation(std::uint32_t width, std::uint32_t height, CaptureTaskLimits limits)
		    : Limits(limits) {
			if (limits.MaximumExtent == 0 || limits.MaximumExtent > 16384 || limits.MaximumConsumers == 0 || limits.MaximumConsumers > 65536 || limits.MaximumFrameReplacements == 0 || limits.MaximumFrameReplacements > 1024 || width == 0 || height == 0 || width > limits.MaximumExtent || height > limits.MaximumExtent) {
				throw std::runtime_error("Capture requires bounded positive actual fixed dimensions and owner capacities");
			}
			State.FixedSize = {static_cast<float>(width), static_cast<float>(height)};
			State.PhysicalBounds = {0, 0, State.FixedSize.X, State.FixedSize.Y};
			Displaced.reserve(limits.MaximumFrameReplacements);
		}
		void RequireIdle() const {
			if (Thread != std::this_thread::get_id() || State.Closed || Drawing || Notifying) {
				throw std::runtime_error("Capture requires its actual live idle owner on the containing frame thread");
			}
		}
		void InvalidateConsumers() noexcept {
			State.Captured = false;
			Notifying = true;
			for (auto* consumer : Consumers) {
				consumer->CaptureInvalidated();
			}
			Notifying = false;
		}
		CaptureTaskLimits Limits;
		CaptureTaskState State;
		std::unique_ptr<MutableTextureOwner> Texture;
		std::vector<std::unique_ptr<MutableTextureOwner>> Displaced;
		std::vector<CaptureTaskObserver*> Consumers;
		const std::thread::id Thread = std::this_thread::get_id();
		bool Drawing = false;
		bool Notifying = false;
	};

	CaptureTask::CaptureTask(std::uint32_t width, std::uint32_t height, CaptureTaskLimits limits)
	    : m_Implementation(std::make_unique<Implementation>(width, height, limits)) {
	}

	CaptureTask::~CaptureTask() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void CaptureTask::SetOffset(resource::MotionPoint offset) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!std::isfinite(offset.X) || !std::isfinite(offset.Y)) {
			throw std::runtime_error("Capture offset requires finite actual coordinates");
		}
		// Native28f080 negates both arguments before virtual4db030 stores the logical coordinate.
		state.State.Coordinate = {-offset.X, -offset.Y};
	}

	void CaptureTask::Draw(MutableTextureBackend& actual_backend, const CaptureFrameContext& frame, const AllocateTexture& allocate, const CopyTexture& copy) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!allocate || !copy) {
			throw std::runtime_error("Capture drawing requires its typed actual texture preparation and same-frame copy services");
		}
		const auto& coordinate = state.State.Coordinate;
		const auto& size = state.State.FixedSize;
		const MotionBounds logical{coordinate.X, coordinate.Y, coordinate.X + size.X, coordinate.Y + size.Y};
		const auto physical = PrepareCapturePhysicalBounds(logical, frame);
		const auto width = physical.MaximumX - physical.MinimumX;
		const auto height = physical.MaximumY - physical.MinimumY;
		if (width > static_cast<float>(state.Limits.MaximumExtent) || height > static_cast<float>(state.Limits.MaximumExtent)) {
			throw std::runtime_error("Capture projected dimensions exceed its actual bounded texture extent");
		}
		const bool replacement = !state.Texture || !SameCaptureBounds(physical, state.State.PhysicalBounds);
		if (replacement && state.Texture && state.Displaced.size() == state.Limits.MaximumFrameReplacements) {
			throw std::runtime_error("Capture bounds replacements exceed its bounded current-frame resource retention");
		}
		if (state.Texture && !state.Texture->UsesBackend(actual_backend)) {
			throw std::runtime_error("Capture current texture belongs to another actual graphics backend");
		}
		state.Drawing = true;
		try {
			if (replacement) {
				MutableTextureSpec spec;
				spec.Storage = MutableTextureStorage::StagingCompanion;
				spec.Description.Width = static_cast<std::uint32_t>(width);
				spec.Description.Height = static_cast<std::uint32_t>(height);
				spec.Description.OriginalFormat = frame.OriginalFormat;
				auto prepared = allocate(spec);
				if (!prepared || !prepared->UsesBackend(actual_backend)) {
					throw std::runtime_error("Capture allocation must return its exact enclosing graphics backend owner");
				}
				const auto actual = prepared->State();
				if (actual.Storage != MutableTextureStorage::StagingCompanion || actual.Description.Width != spec.Description.Width || actual.Description.Height != spec.Description.Height || actual.Description.OriginalFormat != spec.Description.OriginalFormat || actual.Description.MipLevels != 1 || actual.Description.SampleCount != 1 || !actual.HasCompanion || actual.Locked) {
					throw std::runtime_error("Capture allocation requires the genuine single-level staging/companion pair and exact physical descriptor");
				}
				if (state.Texture) {
					state.Displaced.push_back(std::move(state.Texture));
				}
				state.Texture = std::move(prepared);
				state.State.HasTexture = true;
				state.State.PhysicalBounds = physical;
				state.State.FramePending = true;
				for (auto* consumer : state.Consumers) {
					consumer->CaptureResized({width, height});
				}
			}
			state.State.FramePending = true;
			copy(*state.Texture, physical);
			state.State.Captured = true;
			for (auto* consumer : state.Consumers) {
				consumer->CaptureCompleted();
			}
			state.Drawing = false;
		} catch (...) {
			state.Drawing = false;
			if (state.State.FramePending) {
				state.InvalidateConsumers();
			}
			throw;
		}
	}

	void CaptureTask::ReleaseFrame(bool submitted) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.State.FramePending && !submitted) {
			state.InvalidateConsumers();
		}
		state.Displaced.clear();
		state.State.FramePending = false;
	}

	void CaptureTask::AddConsumer(CaptureTaskObserver& consumer) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.Consumers.size() == state.Limits.MaximumConsumers || std::find(state.Consumers.begin(), state.Consumers.end(), &consumer) != state.Consumers.end()) {
			throw std::runtime_error("Capture consumer attachment requires a unique bounded actual MCaptured observer");
		}
		// Native4fac50 reads initial bounds/Captured before appending its backlink; no synthetic Added notification.
		state.Consumers.push_back(&consumer);
	}

	void CaptureTask::RemoveConsumer(CaptureTaskObserver& consumer) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		const auto found = std::find(state.Consumers.begin(), state.Consumers.end(), &consumer);
		if (found == state.Consumers.end()) {
			throw std::runtime_error("Capture consumer removal requires its actual retained backlink");
		}
		state.Consumers.erase(found);
	}

	MutableTextureOwner& CaptureTask::Texture() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.Texture || !state.State.Captured) {
			throw std::runtime_error("Capture texture requires its actual preceding framebuffer copy");
		}
		return *state.Texture;
	}

	CaptureTaskState CaptureTask::State() const noexcept {
		return m_Implementation->State;
	}

	bool CaptureTask::CanClose() const noexcept {
		const auto& state = *m_Implementation;
		return state.Thread == std::this_thread::get_id() && !state.Drawing && !state.Notifying && !state.State.FramePending;
	}

	void CaptureTask::Close() {
		auto& state = *m_Implementation;
		if (state.State.Closed) {
			return;
		}
		state.RequireIdle();
		if (state.State.FramePending) {
			throw std::runtime_error("Capture retirement requires actual containing-frame End or Abort and resource release");
		}
		state.Notifying = true;
		state.State.Closed = true;
		state.State.Captured = false;
		for (auto* consumer : state.Consumers) {
			consumer->CaptureRetired();
		}
		state.Consumers.clear();
		state.Texture.reset();
		state.State.HasTexture = false;
		state.Notifying = false;
	}
} // namespace sgre::scene
