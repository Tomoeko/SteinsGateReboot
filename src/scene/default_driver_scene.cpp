#include "sgre/scene/default_driver_scene.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	class DefaultDriverSceneCall {
	public:
		explicit DefaultDriverSceneCall(bool& calling)
		    : m_Calling(calling) {
			if (calling) {
				throw std::runtime_error("actual SQDriver scene dispatch cannot reenter update or draw");
			}
			m_Calling = true;
		}

		~DefaultDriverSceneCall() {
			m_Calling = false;
		}

	private:
		bool& m_Calling;
	};

	static bool HasActualDriverTaskServices(const DefaultDriverOwnedTaskOperations& task) {
		return task.Update && task.Draw && task.Retire;
	}

	DefaultDriverScene::DefaultDriverScene(CommonProcessTasks& controller, LayerTree& tree, DefaultDriverSceneServices services, std::size_t max_tasks_per_group)
	    : m_Controller(controller), m_Tree(tree), m_ResourceManager(std::move(services.ResourceManager)), m_ResourceMeter(std::move(services.ResourceMeter)), m_Priority(max_tasks_per_group), m_BeforeLayersFirst(max_tasks_per_group), m_BeforeLayersSecond(max_tasks_per_group), m_AfterResourceMeter(max_tasks_per_group), m_RootLayers(tree, {}, std::move(services.RootLayers)) {
		if (!HasActualDriverTaskServices(m_ResourceManager) || !HasActualDriverTaskServices(m_ResourceMeter) || !controller.Parallel().CanClear() || controller.Parallel().Count() != 0 || controller.Parallel().Status() != TaskStatus::Active || controller.Serial().Count() != 0 || tree.ChildCount() != 0) {
			throw std::runtime_error("actual SQDriver setup requires unused native parallel/root owners and concrete ResourceManager/ResourceMeter services");
		}
		tree.RequireCanClearChildren();

		try {
			RegisterRoot(0, OwnedTaskCallbacks(m_ResourceManager));
			RegisterRoot(1, GroupCallbacks(m_Priority));
			RegisterRoot(2, GroupCallbacks(m_BeforeLayersFirst));
			RegisterRoot(3, GroupCallbacks(m_BeforeLayersSecond));
			ParallelTaskCallbacks layers;
			layers.Update = [this](auto) {
				m_RootLayers.Update();
			};
			layers.Draw = [this](auto, std::uint32_t argument) {
				m_RootLayers.Draw(argument);
			};
			layers.Release = [this](auto) {
				m_Tree.ClearRoot();
			};
			RegisterRoot(4, std::move(layers));
			RegisterRoot(5, OwnedTaskCallbacks(m_ResourceMeter), true);
			RegisterRoot(6, GroupCallbacks(m_AfterResourceMeter));
		} catch (...) {
			const auto failure = std::current_exception();
			try {
				Close();
			} catch (...) {
			}

			std::rethrow_exception(failure);
		}
	}

	DefaultDriverScene::~DefaultDriverScene() noexcept {
		try {
			Close();
		} catch (...) {
			if (!m_Closed) {
				// An active or transferred root still captures this graph; destroying it would dangle its callback.
				std::terminate();
			}
		}
	}

	void DefaultDriverScene::RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks, bool draw_paused) {
		auto retire = std::move(callbacks.Release);
		callbacks.Release = [this, index, retire = std::move(retire)](auto task) {
			m_RootReleased[index] = true;
			if (retire) {
				retire(task);
			}
		};
		ParallelTaskState state;
		state.DrawPaused = draw_paused;
		m_Roots[index] = m_Controller.Parallel().Append(std::move(callbacks), state);
	}

	ParallelTaskCallbacks DefaultDriverScene::GroupCallbacks(ParallelTaskSet& group) {
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
		callbacks.Release = [&group](auto) {
			group.Clear();
		};

		return callbacks;
	}

	ParallelTaskCallbacks DefaultDriverScene::OwnedTaskCallbacks(DefaultDriverOwnedTaskOperations& task) {
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [&task](auto) {
			task.Update();
		};
		callbacks.Draw = [&task](auto, std::uint32_t argument) {
			task.Draw(argument);
		};
		callbacks.Release = [&task](auto) {
			task.Retire();
		};

		return callbacks;
	}

	void DefaultDriverScene::RequireOpen() const {
		if (m_Closed) {
			throw std::runtime_error("actual SQDriver scene graph has retired");
		}
		for (const auto root : m_Roots) {
			if (!m_Controller.Parallel().Contains(root)) {
				throw std::runtime_error("actual SQDriver roots must remain registered through scene dispatch");
			}
		}
	}

	ParallelTaskSet& DefaultDriverScene::Group(DefaultDriverTaskGroup group) {
		RequireOpen();
		switch (group) {
		case DefaultDriverTaskGroup::Priority:
			return m_Priority;
		case DefaultDriverTaskGroup::BeforeLayersFirst:
			return m_BeforeLayersFirst;
		case DefaultDriverTaskGroup::BeforeLayersSecond:
			return m_BeforeLayersSecond;
		case DefaultDriverTaskGroup::AfterResourceMeter:
			return m_AfterResourceMeter;
		}

		throw std::runtime_error("actual SQDriver task group is outside its recovered four owners");
	}

	void DefaultDriverScene::Update(const std::function<void()>& actual_driver_phase) {
		const DefaultDriverSceneCall call(m_Calling);
		RequireOpen();
		m_Controller.Update(actual_driver_phase);
	}

	void DefaultDriverScene::Draw(std::uint32_t argument) {
		const DefaultDriverSceneCall call(m_Calling);
		RequireOpen();
		m_Controller.Draw(argument);
	}

	bool DefaultDriverScene::CanClose() const noexcept {
		if (m_Closed) {
			return true;
		}
		if (m_Calling || !m_Controller.Parallel().CanClear() || !m_Priority.CanClear() || !m_BeforeLayersFirst.CanClear() || !m_BeforeLayersSecond.CanClear() || !m_AfterResourceMeter.CanClear()) {
			return false;
		}
		try {
			m_Tree.RequireCanClearChildren();
		} catch (...) {
			return false;
		}
		for (std::size_t index = 0; index != m_Roots.size(); ++index) {
			const auto root = m_Roots[index];
			if (m_Controller.Parallel().Contains(root)) {
				if (!m_Controller.Parallel().CanExtract(root)) {
					return false;
				}
			} else if (root.Owner != nullptr && !m_RootReleased[index]) {
				return false;
			}
		}

		return true;
	}

	void DefaultDriverScene::Close() {
		if (m_Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("actual SQDriver scene cannot retire active groups/Layer traversal or transferred root ownership");
		}

		try {
			m_Controller.Parallel().Clear();
		} catch (...) {
			m_Closed = true;

			throw;
		}
		m_Closed = true;
	}

} // namespace sgre::scene
