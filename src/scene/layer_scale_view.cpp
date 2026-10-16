#include "sgre/scene/layer_scale_view.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	struct ScaleViewTask {
		LayerScaleTaskCallbacks Callbacks;
		ParallelTaskSet* Owner = nullptr;
		ParallelTaskHandle Handle;
		bool Alive = true;
	};

	struct ScaleViewCamera {
		explicit ScaleViewCamera(std::size_t capacity)
		    : Children(capacity) {
		}

		LayerScaleCameraState State;
		ParallelTaskSet Children;
		ParallelTaskHandle Handle;
		std::weak_ptr<ScaleViewTask> Task;
		bool Alive = true;
	};

	static ParallelTaskCallbacks TaskRegistration(const std::shared_ptr<ScaleViewTask>& task) {
		ParallelTaskCallbacks registration;
		registration.Update = [task](auto) {
			if (task->Callbacks.Update) {
				task->Callbacks.Update();
			}
		};
		registration.Draw = [task](auto, auto argument) {
			if (task->Callbacks.Draw) {
				task->Callbacks.Draw(argument);
			}
		};
		registration.Release = [task](auto) {
			task->Alive = false;
			task->Owner = nullptr;
			std::exception_ptr failure;
			try {
				if (task->Callbacks.Release) {
					task->Callbacks.Release();
				}
			} catch (...) {
				failure = std::current_exception();
			}

			task->Callbacks = {};
			if (failure) {
				std::rethrow_exception(failure);
			}
		};

		registration.CanRelease = [task](auto) {
			return !task->Callbacks.CanRelease || task->Callbacks.CanRelease();
		};

		return registration;
	}

	static ParallelTaskCallbacks CameraRegistration(const std::shared_ptr<ScaleViewCamera>& camera) {
		ParallelTaskCallbacks registration;
		registration.Update = [camera](auto) {
			camera->Children.Update();
		};
		registration.Draw = [camera](auto, auto argument) {
			const auto task = camera->Task.lock();
			if (task && task->Alive && task->Callbacks.DrawWithCamera) {
				task->Callbacks.DrawWithCamera(camera->State, camera->Children, argument);
			} else if (task && task->Alive && task->Callbacks.Draw) {
				throw std::runtime_error("scaled Layer view drawing requires its recovered matrix/scissor scope");
			} else {
				camera->Children.Draw(argument);
			}
		};
		registration.Release = [camera](auto) {
			camera->Alive = false;
			camera->Children.Clear();
		};
		registration.CanRelease = [camera](auto) {
			return camera->Children.CanClear();
		};

		return registration;
	}

	class LayerScaleView::Implementation {
	public:
		Implementation(MotionLayerTasks& parent, LayerScaleTaskCallbacks callbacks, std::size_t max_camera_tasks)
		    : Parent(&parent.Group(MotionTaskGroup::First)), CameraCapacity(max_camera_tasks) {
			if (max_camera_tasks == 0 || max_camera_tasks > 65536) {
				throw std::runtime_error("Layer scale camera requires a positive bounded child capacity");
			}

			auto task = std::make_shared<ScaleViewTask>();
			task->Callbacks = std::move(callbacks);
			task->Owner = Parent;
			task->Handle = Parent->Append(TaskRegistration(task), {TaskStatus::Active, task->Callbacks.InitiallyDrawPaused});
			Task = task;
		}

		ParallelTaskSet* Parent;
		std::weak_ptr<ScaleViewTask> Task;
		std::weak_ptr<ScaleViewCamera> Camera;
		LayerScaleViewState View;
		std::size_t CameraCapacity;

		std::shared_ptr<ScaleViewTask> RequireTask() const {
			const auto task = Task.lock();
			if (View.Detached || !task || !task->Alive || !task->Owner || !task->Owner->Contains(task->Handle)) {
				throw std::runtime_error("Layer scale view has no live attached task owner");
			}

			return task;
		}

		std::shared_ptr<ScaleViewCamera> EnsureCamera() {
			if (const auto existing = Camera.lock()) {
				if (!existing->Alive || !Parent || !Parent->Contains(existing->Handle)) {
					throw std::runtime_error("Layer scale view camera registration has expired");
				}

				return existing;
			}

			const auto task = RequireTask();
			if (!Parent || task->Owner != Parent || !Parent->CanExtract(task->Handle)) {
				throw std::runtime_error("lazy Layer camera transfer requires a stable original First registration");
			}

			auto camera = std::make_shared<ScaleViewCamera>(CameraCapacity);
			camera->Task = task;
			auto registration = CameraRegistration(camera);
			auto lease = Parent->Extract(task->Handle);
			camera->Handle = Parent->Append(std::move(registration));
			task->Handle = camera->Children.AppendLease(std::move(lease));
			task->Owner = &camera->Children;
			Camera = camera;
			View.HasCamera = true;

			return camera;
		}
	};

	LayerScaleView::LayerScaleView(MotionLayerTasks& parent, LayerScaleTaskCallbacks task, std::size_t max_camera_tasks)
	    : m_Implementation(std::make_unique<Implementation>(parent, std::move(task), max_camera_tasks)) {
	}

	LayerScaleView::~LayerScaleView() {
		try {
			Retire();
		} catch (...) {
			// Parent ownership remains responsible for eventual concrete task cleanup.
		}
	}

	LayerScaleViewState LayerScaleView::State() const noexcept {
		auto state = m_Implementation->View;
		const auto camera = m_Implementation->Camera.lock();
		state.HasCamera = camera && camera->Alive;

		return state;
	}

	ParallelTaskState LayerScaleView::TaskState() const {
		const auto task = m_Implementation->RequireTask();

		return task->Owner->State(task->Handle);
	}

	LayerScaleCameraState LayerScaleView::CameraState() const {
		static_cast<void>(m_Implementation->RequireTask());
		const auto camera = m_Implementation->Camera.lock();
		if (!camera || !camera->Alive) {
			throw std::runtime_error("Layer scale view has not created its camera");
		}

		return camera->State;
	}

	void LayerScaleView::SetOffset(resource::MotionPoint offset) {
		if (m_Implementation->View.Detached) {
			return;
		}

		static_cast<void>(m_Implementation->RequireTask());
		if (offset.X != 0 || offset.Y != 0 || m_Implementation->View.HasCamera) {
			m_Implementation->EnsureCamera()->State.Offset = offset;
		}

		m_Implementation->View.Offset = offset;
	}

	void LayerScaleView::SetScale(resource::MotionPoint scale) {
		if (m_Implementation->View.Detached) {
			return;
		}

		static_cast<void>(m_Implementation->RequireTask());
		if (scale.X != 1 || scale.Y != 1 || m_Implementation->View.HasCamera) {
			m_Implementation->EnsureCamera()->State.Scale = scale;
		}

		m_Implementation->View.Scale = scale;
	}

	void LayerScaleView::SetPrimaryScale(float scale) {
		SetScale({scale, scale});
	}

	void LayerScaleView::SetRotation(float degrees) {
		if (m_Implementation->View.Detached) {
			return;
		}

		static_cast<void>(m_Implementation->RequireTask());
		if (degrees != 0 || m_Implementation->View.HasCamera) {
			m_Implementation->EnsureCamera()->State.RotationRadians = degrees * 0.0174532924f;
		}
	}

	void LayerScaleView::SetVisible(bool visible) {
		if (m_Implementation->View.Detached) {
			return;
		}

		const auto task = m_Implementation->RequireTask();
		task->Owner->SetDrawPaused(task->Handle, !visible);
	}

	void LayerScaleView::Retire() {
		if (m_Implementation->View.Detached) {
			return;
		}

		if (const auto task = m_Implementation->Task.lock(); task && task->Alive && task->Owner && task->Owner->Contains(task->Handle)) {
			task->Owner->SetStatus(task->Handle, TaskStatus::Retire);
		}
		if (const auto camera = m_Implementation->Camera.lock(); camera && camera->Alive && m_Implementation->Parent && m_Implementation->Parent->Contains(camera->Handle)) {
			m_Implementation->Parent->SetStatus(camera->Handle, TaskStatus::Retire);
		}
	}

	void LayerScaleView::DetachParent() noexcept {
		m_Implementation->View.Detached = true;
		m_Implementation->View.HasCamera = false;
		m_Implementation->Parent = nullptr;
		m_Implementation->Task.reset();
		m_Implementation->Camera.reset();
	}

} // namespace sgre::scene
