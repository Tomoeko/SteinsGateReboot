#include "sgre/scene/raw_layer_owner.hpp"

#include "sgre/scene/raw_layer_frame.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	RawLayerOwner::RawLayerOwner(LayerTree& tree, std::unique_ptr<RawTextureTask> texture_task, ParallelTaskSet& background_driver, ParallelTaskHandle background_task, RawLayerRetirement retirement, RawLayerAttachment attachment, std::size_t max_tasks_per_group, SerialTaskSet::Completed completed) : m_Tree(tree), m_BackgroundDriver(background_driver), m_BackgroundTask(background_task), m_CommonFirst(max_tasks_per_group), m_CommonSecond(max_tasks_per_group), m_Process(max_tasks_per_group, std::move(completed)), m_TextureTask(std::move(texture_task)), m_Retirement(std::move(retirement)) {
		if (!m_TextureTask || m_TextureTask->State().Closed || !m_BackgroundDriver.Contains(background_task) || !m_Retirement.DropParent || !m_Retirement.ReleaseObject || (attachment.Folder && attachment.Screen)) {
			throw std::runtime_error("RawTex common owner requires a live texture, driver task, retirement bridge and one parent manager");
		}

		m_CommonFirst.SetRetireWhenEmpty(false);
		m_CommonSecond.SetRetireWhenEmpty(false);
		m_Handle = attachment.Screen ? m_Tree.CreateRawTextureInScreen(*attachment.Screen) : m_Tree.CreateRawTexture(attachment.Folder);
	}

	RawLayerOwner::~RawLayerOwner() {
		if (m_Frame != nullptr) {
			// The concrete frame borrows this owner and must detach before owner destruction.
			std::terminate();
		}

		try {
			Close();
		} catch (...) {
			// Explicit Close reports the first failure after valid ordered ownership cleanup.
		}
	}

	void RawLayerOwner::RequireOpen() const {
		if (m_Closed) {
			throw std::runtime_error("RawTex common layer owner has retired");
		}
	}

	LayerHandle RawLayerOwner::Handle() const noexcept {
		return m_Handle;
	}

	bool RawLayerOwner::IsClosed() const noexcept {
		return m_Closed;
	}

	bool RawLayerOwner::CanClose() const noexcept {
		if (m_Closed) {
			return true;
		}
		if (!m_CommonFirst.CanClear() || !m_CommonSecond.CanClear() || !m_Process.CanClear() || !m_TextureTask->CanClose() || (m_Frame != nullptr && !m_Frame->CanClose())) {
			return false;
		}
		try {
			m_Tree.RequireCanRelease(m_Handle);
		} catch (...) {
			return false;
		}

		return true;
	}

	RawTextureTask& RawLayerOwner::TextureTask() {
		RequireOpen();

		return *m_TextureTask;
	}

	ParallelTaskSet& RawLayerOwner::Group(RawLayerTaskGroup group) {
		RequireOpen();
		switch (group) {
		case RawLayerTaskGroup::CommonFirst:
			return m_CommonFirst;
		case RawLayerTaskGroup::CommonSecond:
			return m_CommonSecond;
		}

		throw std::runtime_error("RawTex common task group is outside its two-group layout");
	}

	SerialTaskSet& RawLayerOwner::SerialTasks() {
		RequireOpen();

		return m_Process.Serial();
	}

	CommonProcessTasks& RawLayerOwner::CommonTasks() {
		RequireOpen();

		return m_Process;
	}

	void RawLayerOwner::Close() {
		if (m_Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("RawTex common ownership cannot retire during an active task callback");
		}

		m_Closed = true;
		std::exception_ptr failure;
		const auto perform = [&failure](auto operation) {
			try {
				operation();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		};
		perform([&] {
			m_BackgroundDriver.SetStatus(m_BackgroundTask, TaskStatus::Retire);
		});
		perform([&] {
			m_Tree.Detach(m_Handle);
		});
		perform([&] {
			m_Retirement.DropParent();
		});
		perform([&] {
			m_Process.ClearParallel();
		});
		perform([&] {
			m_CommonFirst.Clear();
		});
		perform([&] {
			m_CommonSecond.Clear();
		});
		perform([&] {
			m_TextureTask->Close();
		});
		m_TextureTask.reset();
		perform([&] {
			m_Process.ClearSerial();
		});
		perform([&] {
			m_Retirement.ReleaseObject();
		});
		perform([&] {
			m_Tree.Release(m_Handle);
		});
		m_Retirement = {};

		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::scene
