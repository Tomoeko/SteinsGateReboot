#include "sgre/scene/layer_folder_frame.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	LayerFolderTasks::LayerFolderTasks(std::size_t max_tasks_per_group)
	    : m_CommonFirst(max_tasks_per_group), m_CommonSecond(max_tasks_per_group) {
	}

	ParallelTaskSet& LayerFolderTasks::CommonFirst() noexcept {
		return m_CommonFirst;
	}

	ParallelTaskSet& LayerFolderTasks::CommonSecond() noexcept {
		return m_CommonSecond;
	}

	bool LayerFolderTasks::CanClear() const noexcept {
		return m_CommonFirst.CanClear() && m_CommonSecond.CanClear();
	}

	void LayerFolderTasks::Clear() {
		if (!CanClear()) {
			throw std::runtime_error("actual Folder task retirement requires both inactive native common groups");
		}

		std::exception_ptr failure;
		try {
			m_CommonFirst.Clear();
		} catch (...) {
			failure = std::current_exception();
		}
		try {
			m_CommonSecond.Clear();
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	// SQLayerFolder MProcess vtable +0x30 is the exact return-zero body 0x140030f20.
	static void NativeFolderOnProcess() {
	}

	LayerFolderFrame::LayerFolderFrame(CommonProcessTasks& process, LayerFolderTasks& groups, LayerTree& tree, LayerHandle folder, LayerCameraDrawScope& renderer, ReadState state, LayerManagerFrameOperations children, std::function<void()> clear_children)
	    : m_Process(process), m_Groups(groups), m_Renderer(renderer), m_ReadState(std::move(state)), m_ClearChildren(std::move(clear_children)), m_Children(tree, folder, std::move(children), LayerManagerDrawMode::PreserveRenderer) {
		if (tree.State(folder).Kind != LayerKind::Folder || !m_ReadState || !m_ClearChildren || !process.CanClear() || !groups.CanClear() || process.Parallel().Count() != 0 || process.Parallel().Status() != TaskStatus::Active) {
			throw std::runtime_error("actual Folder frame requires its unused common root, actual Folder groups and concrete state/child services");
		}

		ParallelTaskCallbacks child_manager;
		child_manager.Update = [this](auto) {
			m_Children.Update();
		};
		child_manager.Draw = [this](auto, std::uint32_t argument) {
			m_Children.Draw(argument);
		};
		static_cast<void>(m_CameraChildren.Append(std::move(child_manager)));
		ParallelTaskCallbacks camera;
		camera.Update = [this](auto) {
			m_CameraChildren.Update();
		};
		camera.Draw = [this](auto, std::uint32_t argument) {
			m_Renderer.Draw(m_ReadState().Camera, m_CameraChildren, argument);
		};
		static_cast<void>(m_BaseClipChildren.Append(std::move(camera)));

		try {
			RegisterRoot(0, GroupCallbacks(groups.CommonFirst()));
			RegisterRoot(1, GroupCallbacks(groups.CommonSecond()));
			ParallelTaskCallbacks clip;
			clip.Update = [this](auto) {
				m_BaseClipChildren.Update();
			};
			clip.Draw = [this](auto, std::uint32_t argument) {
				m_Renderer.DrawClip(m_ReadState().BaseClip, m_BaseClipChildren, argument);
			};
			RegisterRoot(2, std::move(clip));
		} catch (...) {
			Close();

			throw;
		}
	}

	LayerFolderFrame::~LayerFolderFrame() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void LayerFolderFrame::RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks) {
		callbacks.Release = [this, index](auto) {
			m_RootReleased[index] = true;
			if (m_Detaching) {
				return;
			}

			if (index == 0) {
				m_Groups.CommonFirst().Clear();
			} else if (index == 1) {
				m_Groups.CommonSecond().Clear();
			} else {
				m_ClearChildren();
			}
		};
		m_Roots[index] = m_Process.Parallel().Append(std::move(callbacks));
	}

	ParallelTaskCallbacks LayerFolderFrame::GroupCallbacks(ParallelTaskSet& group) {
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

	void LayerFolderFrame::RequireOpen() const {
		if (m_Closed) {
			throw std::runtime_error("actual Folder frame attachment has retired");
		}
		for (const auto root : m_Roots) {
			if (!m_Process.Parallel().Contains(root)) {
				throw std::runtime_error("actual Folder roots must remain registered through frame dispatch");
			}
		}
	}

	void LayerFolderFrame::Update() {
		RequireOpen();
		m_Process.Update(NativeFolderOnProcess);
	}

	void LayerFolderFrame::Draw(std::uint32_t argument) {
		RequireOpen();
		m_Process.Draw(argument);
	}

	bool LayerFolderFrame::CanClose() const noexcept {
		if (m_Closed) {
			return true;
		}
		if (!m_Process.CanClear() || !m_Groups.CanClear() || !m_BaseClipChildren.CanClear() || !m_CameraChildren.CanClear()) {
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

	void LayerFolderFrame::Close() {
		if (m_Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("actual Folder frame cannot detach during traversal or while transferred adapters remain alive");
		}

		m_Detaching = true;
		for (const auto root : m_Roots) {
			if (m_Process.Parallel().Contains(root)) {
				auto adapter = m_Process.Parallel().Extract(root);
				adapter.Release();
			}
		}
		m_Detaching = false;
		m_Closed = true;
	}

	void LayerFolderFrame::RetireParallel() {
		if (!CanClose()) {
			throw std::runtime_error("actual Folder parallel retirement requires its stable live concrete graph");
		}

		try {
			m_Process.ClearParallel();
		} catch (...) {
			m_Closed = true;

			throw;
		}
		m_Closed = true;
	}

} // namespace sgre::scene
