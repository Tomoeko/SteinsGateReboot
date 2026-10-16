#include "sgre/scene/raw_layer_frame.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	RawLayerFrame::RawLayerFrame(RawLayerOwner& owner, LayerCameraDrawScope& renderer, ReadState state, DrawTexture drawing) : m_Owner(owner), m_Process(owner.CommonTasks()), m_First(owner.Group(RawLayerTaskGroup::CommonFirst)), m_Second(owner.Group(RawLayerTaskGroup::CommonSecond)), m_Texture(owner.TextureTask()), m_Renderer(renderer), m_ReadState(std::move(state)), m_DrawTexture(std::move(drawing)) {
		if (owner.m_Frame != nullptr || !m_ReadState || !m_DrawTexture || !owner.CanClose() || m_Process.Parallel().Count() != 0 || m_Process.Parallel().Status() != TaskStatus::Active) {
			throw std::runtime_error("actual Raw frame requires one unused common root and concrete camera/mutable-texture drawing services");
		}

		ParallelTaskCallbacks texture;
		texture.Update = [this](auto) {
			m_Texture.Update();
		};
		texture.Draw = [this](auto, std::uint32_t argument) {
			const auto state = m_Texture.State();
			if (!state.Task.DrawPaused && state.Task.Status != TaskStatus::Retire) {
				const auto packet = m_Texture.PrepareDrawPacket();
				m_DrawTexture(m_Texture, packet, argument);
			}
		};
		static_cast<void>(m_CameraChildren.Append(std::move(texture)));
		ParallelTaskCallbacks camera;
		camera.Update = [this](auto) {
			m_CameraChildren.Update();
		};
		camera.Draw = [this](auto, std::uint32_t argument) {
			m_Renderer.Draw(m_ReadState().Camera, m_CameraChildren, argument);
		};
		static_cast<void>(m_BaseClipChildren.Append(std::move(camera)));

		try {
			RegisterRoot(0, GroupCallbacks(m_First));
			RegisterRoot(1, GroupCallbacks(m_Second));
			ParallelTaskCallbacks clip;
			clip.Update = [this](auto) {
				m_BaseClipChildren.Update();
			};
			clip.Draw = [this](auto, std::uint32_t argument) {
				m_Renderer.DrawClip(m_ReadState().BaseClip, m_BaseClipChildren, argument);
			};
			RegisterRoot(2, std::move(clip));
			owner.m_Frame = this;
		} catch (...) {
			const auto failure = std::current_exception();
			Close();

			std::rethrow_exception(failure);
		}
	}

	RawLayerFrame::~RawLayerFrame() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	ParallelTaskCallbacks RawLayerFrame::GroupCallbacks(ParallelTaskSet& group) {
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [&group](auto) {
			if (group.Status() == TaskStatus::Active) {
				group.Update();
			}
		};
		callbacks.Draw = [&group](auto, std::uint32_t argument) {
			if (group.Status() != TaskStatus::Retire) {
				group.Draw(argument);
			}
		};

		return callbacks;
	}

	void RawLayerFrame::RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks) {
		callbacks.Release = [this, index](auto) {
			m_RootReleased[index] = true;
			if (m_Detaching) {
				return;
			}
			if (index == 0) {
				m_First.Clear();
			} else if (index == 1) {
				m_Second.Clear();
			} else {
				m_Texture.Close();
			}
		};
		m_Roots[index] = m_Process.Parallel().Append(std::move(callbacks));
	}

	void RawLayerFrame::RequireOpen() const {
		if (m_Closed || m_Owner.IsClosed() || m_Owner.m_Frame != this) {
			throw std::runtime_error("actual Raw frame attachment has retired or detached");
		}
		for (const auto root : m_Roots) {
			if (!m_Process.Parallel().Contains(root)) {
				throw std::runtime_error("actual Raw common roots must remain registered through frame dispatch");
			}
		}
	}

	void RawLayerFrame::Update(const std::function<void()>& on_process) {
		RequireOpen();
		m_Process.Update(on_process);
	}

	void RawLayerFrame::Draw(std::uint32_t argument) {
		RequireOpen();
		m_Process.Draw(argument);
	}

	bool RawLayerFrame::CanClose() const noexcept {
		if (m_Closed) {
			return true;
		}
		if (m_Owner.IsClosed()) {
			return m_Process.CanClear() && m_BaseClipChildren.CanClear() && m_CameraChildren.CanClear();
		}
		if (!m_Process.CanClear() || !m_First.CanClear() || !m_Second.CanClear() || !m_Texture.CanClose() || !m_BaseClipChildren.CanClear() || !m_CameraChildren.CanClear()) {
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

	void RawLayerFrame::Close() {
		if (m_Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("actual Raw frame cannot detach during traversal or transferred adapter ownership");
		}

		// Owner Close clears the graph before destroying the borrowed texture task.
		if (!m_Owner.IsClosed()) {
			m_Detaching = true;
			for (const auto root : m_Roots) {
				if (m_Process.Parallel().Contains(root)) {
					auto adapter = m_Process.Parallel().Extract(root);
					adapter.Release();
				}
			}
			m_Detaching = false;
		}
		if (m_Owner.m_Frame == this) {
			m_Owner.m_Frame = nullptr;
		}
		m_Closed = true;
	}

} // namespace sgre::scene
