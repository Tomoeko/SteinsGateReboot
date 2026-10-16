#include "sgre/scene/motion_layer_frame.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	MotionLayerFrame::MotionLayerFrame(CommonProcessTasks& process, MotionLayerTasks& groups, LayerCameraDrawScope& renderer, ReadState state, MotionLayerSourceDrawing drawing)
	    : m_Process(process), m_Groups(groups), m_Renderer(renderer), m_ReadState(std::move(state)), m_Drawing(std::move(drawing)) {
		if (!m_ReadState || !m_Drawing.Secondary || !m_Drawing.Primary || !process.CanClear() || process.Parallel().Count() != 0 || process.Parallel().Status() != TaskStatus::Active) {
			throw std::runtime_error("actual Motion Layer frame requires its unused common root, real state reader and both source drawing operations");
		}

		static_cast<void>(m_ObjectClipChildren.Append(GroupCallbacks(MotionTaskGroup::First)));
		static_cast<void>(m_CameraChildren.Append(GroupCallbacks(MotionTaskGroup::Second)));
		ParallelTaskCallbacks secondary;
		// The native source-manager update virtual is ret 0. Only its concrete Draw requires work.
		secondary.Draw = [this](auto, std::uint32_t argument) {
			m_Drawing.Secondary(argument);
		};
		static_cast<void>(m_CameraChildren.Append(std::move(secondary)));
		ParallelTaskCallbacks object_clip;
		object_clip.Update = [this](auto) {
			m_ObjectClipChildren.Update();
		};
		object_clip.Draw = [this](auto, std::uint32_t argument) {
			m_Renderer.DrawClip(m_ReadState().ObjectClip, m_ObjectClipChildren, argument);
		};
		static_cast<void>(m_CameraChildren.Append(std::move(object_clip)));
		ParallelTaskCallbacks primary;
		primary.Draw = [this](auto, std::uint32_t argument) {
			m_Drawing.Primary(argument);
		};
		static_cast<void>(m_CameraChildren.Append(std::move(primary)));
		ParallelTaskCallbacks camera;
		camera.Update = [this](auto) {
			m_CameraChildren.Update();
		};
		camera.Draw = [this](auto, std::uint32_t argument) {
			m_Renderer.Draw(m_ReadState().Camera, m_CameraChildren, argument);
		};
		static_cast<void>(m_BaseClipChildren.Append(std::move(camera)));

		try {
			RegisterRoot(0, GroupCallbacks(MotionTaskGroup::CommonFirst));
			RegisterRoot(1, GroupCallbacks(MotionTaskGroup::CommonSecond));
			ParallelTaskCallbacks base_clip;
			base_clip.Update = [this](auto) {
				m_BaseClipChildren.Update();
			};
			base_clip.Draw = [this](auto, std::uint32_t argument) {
				m_Renderer.DrawClip(m_ReadState().BaseClip, m_BaseClipChildren, argument);
			};
			RegisterRoot(2, std::move(base_clip));
		} catch (...) {
			Close();

			throw;
		}
	}

	MotionLayerFrame::~MotionLayerFrame() noexcept {
		try {
			Close();
		} catch (...) {
			// Clearing an active nested set cannot safely detach its executing callbacks.
			std::terminate();
		}
	}

	void MotionLayerFrame::RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks) {
		callbacks.Release = [this, index](auto) {
			m_RootReleased[index] = true;
		};
		m_Roots[index] = m_Process.Parallel().Append(std::move(callbacks));
	}

	ParallelTaskCallbacks MotionLayerFrame::GroupCallbacks(MotionTaskGroup group) {
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [this, group](auto) {
			auto& tasks = m_Groups.Group(group);
			if (tasks.Status() == TaskStatus::Active) {
				tasks.Update();
			}
		};
		callbacks.Draw = [this, group](auto, std::uint32_t argument) {
			auto& tasks = m_Groups.Group(group);
			if (tasks.Status() != TaskStatus::Retire) {
				tasks.Draw(argument);
			}
		};

		return callbacks;
	}

	void MotionLayerFrame::RequireOpen() const {
		if (m_Closed) {
			throw std::runtime_error("actual Motion Layer frame attachment has retired");
		}
		for (const auto root : m_Roots) {
			if (!m_Process.Parallel().Contains(root)) {
				throw std::runtime_error("actual Motion Layer frame root adapters must remain registered until Close");
			}
		}
	}

	// SQLayer MProcess vtable5f6a88+30 points to the return-only030f20 body.
	static void NativeMotionOnProcess() {
	}

	void MotionLayerFrame::Update() {
		RequireOpen();
		m_Process.Update(NativeMotionOnProcess);
	}

	void MotionLayerFrame::Update(const std::function<void()>& on_process) {
		RequireOpen();
		m_Process.Update(on_process);
	}

	void MotionLayerFrame::Draw(std::uint32_t argument) {
		RequireOpen();
		m_Process.Draw(argument);
	}

	bool MotionLayerFrame::CanClose() const noexcept {
		if (m_Closed) {
			return true;
		}
		if (!m_Process.CanClear() || !m_BaseClipChildren.CanClear() || !m_CameraChildren.CanClear() || !m_ObjectClipChildren.CanClear()) {
			return false;
		}

		for (std::size_t index = 0; index != m_Roots.size(); ++index) {
			const auto root = m_Roots[index];
			if (m_Process.Parallel().Contains(root)) {
				if (!m_Process.Parallel().CanExtract(root)) {
					return false;
				}
			} else if (root.Owner != nullptr && !m_RootReleased[index]) {
				return false;
			}
		}

		return true;
	}

	void MotionLayerFrame::Close() {
		if (m_Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("actual Motion Layer frame cannot detach during an active owned traversal or while detached adapters remain alive");
		}

		for (const auto root : m_Roots) {
			if (m_Process.Parallel().Contains(root)) {
				auto adapter = m_Process.Parallel().Extract(root);
				adapter.Release();
			}
		}

		m_Closed = true;
	}

} // namespace sgre::scene
