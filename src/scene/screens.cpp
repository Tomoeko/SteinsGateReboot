#include "sgre/scene/screens.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct ScreenRecord {
		ScreenState State;
		std::shared_ptr<render::SpritePassRenderer> Renderer;
		std::unique_ptr<LayerManagerFrame> Frame;
		ParallelTaskHandle Task;
		std::uint64_t Generation = 1;
		bool Active = false;
		bool PayloadActive = false;
		bool Delivering = false;
	};

	class ScreenRegistryOperation {
	public:
		explicit ScreenRegistryOperation(bool& operating)
		    : m_Operating(operating) {
			if (operating) {
				throw std::runtime_error("Screen owner creation and retirement cannot reenter");
			}
			m_Operating = true;
		}

		~ScreenRegistryOperation() {
			m_Operating = false;
		}

	private:
		bool& m_Operating;
	};

	class ScreenRegistry::Implementation {
	public:
		Implementation(LayerTree& tree, TargetFactory factory, ScreenRegistryLimits limits)
		    : Tree(tree), Factory(std::move(factory)), Limits(limits) {
			if (!Factory || limits.MaxScreens == 0 || limits.MaxScreens > 64 || limits.MaxDimension == 0 || limits.MaxDimension > 16384 || limits.MaxPixels == 0) {
				throw std::runtime_error("screen registry requires an actual target factory and bounded positive limits");
			}

			Records.resize(limits.MaxScreens);
			Order.reserve(limits.MaxScreens);
		}

		LayerTree& Tree;
		TargetFactory Factory;
		ScreenRegistryLimits Limits;
		std::vector<ScreenRecord> Records;
		std::vector<std::uint32_t> Order;
		std::optional<ScreenDriverServices> Services;
		ParallelTaskSet* Driver = nullptr;
		std::thread::id OwnerThread = std::this_thread::get_id();
		std::uint64_t PixelCount = 0;
		bool EverCreated = false;
		bool Operating = false;
		bool Closed = false;

		void RequireThread() const {
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("Screen registry requires its actual serialized owner thread");
			}
		}

		void RequireOpen() const {
			RequireThread();
			if (Closed) {
				throw std::runtime_error("Screen registry has retired");
			}
		}

		bool Contains(ScreenHandle screen) const {
			return !Closed && screen.Owner == this && screen.Index < Records.size() && Records[screen.Index].Active && Records[screen.Index].Generation == screen.Generation;
		}

		ScreenRecord& Record(ScreenHandle screen) {
			RequireOpen();
			if (!Contains(screen)) {
				throw std::runtime_error("screen handle is stale or belongs to another registry");
			}

			return Records[screen.Index];
		}

		const ScreenRecord& Record(ScreenHandle screen) const {
			RequireOpen();
			if (!Contains(screen)) {
				throw std::runtime_error("screen handle is stale or belongs to another registry");
			}

			return Records[screen.Index];
		}

		ScreenHandle Handle(std::uint32_t index) const {
			return {index, Records[index].Generation, this};
		}

		ScreenRecord& Payload(ScreenHandle handle, ParallelTaskHandle task) {
			RequireOpen();
			if (handle.Owner != this || handle.Index >= Records.size()) {
				throw std::runtime_error("Screen payload callback belongs to another registry");
			}
			auto& record = Records[handle.Index];
			if (!record.PayloadActive || record.Generation != handle.Generation || record.Task != task) {
				throw std::runtime_error("Screen payload callback lost its exact native task registration");
			}

			return record;
		}

		void RequirePayloadIdle(const ScreenRecord& record) const {
			if (record.Delivering || record.Generation == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("Screen payload cannot retire its current frame or exhausted identity");
			}
			Tree.RequireCanReleaseScreenRoot(record.State.LayerRoot);
		}

		bool CanReleasePayload(ScreenHandle handle, ParallelTaskHandle task) const noexcept {
			try {
				RequireOpen();
				if (handle.Owner != this || handle.Index >= Records.size()) {
					return false;
				}
				const auto& record = Records[handle.Index];
				if (!record.PayloadActive || record.Generation != handle.Generation || record.Task != task) {
					return false;
				}
				RequirePayloadIdle(record);

				return true;
			} catch (...) {
				return false;
			}
		}

		void RemoveVisible(std::uint32_t index) {
			auto& record = Records[index];
			if (record.Active) {
				Order.erase(std::find(Order.begin(), Order.end(), index));
				record.Active = false;
			}
		}

		void DestroyPayload(std::uint32_t index) {
			auto& record = Records[index];
			RequirePayloadIdle(record);
			RemoveVisible(index);
			// Native4fa170 releases target +38 before the manager +28. The group owns this delayed retirement.
			record.Renderer.reset();
			record.Frame.reset();
			Tree.ReleaseScreenRoot(record.State.LayerRoot);
			PixelCount -= static_cast<std::uint64_t>(record.State.Description.Width) * record.State.Description.Height;
			record.State = {};
			record.Task = {};
			record.PayloadActive = false;
			++record.Generation;
		}

		ParallelTaskCallbacks Callbacks(ScreenHandle handle) {
			ParallelTaskCallbacks callbacks;
			callbacks.Update = [this, handle](auto task) {
				auto& record = Payload(handle, task);
				const ScreenRegistryOperation delivery(record.Delivering);
				record.Frame->Update();
			};
			callbacks.Draw = [this, handle](auto task, auto argument) {
				auto& record = Payload(handle, task);
				const ScreenRegistryOperation delivery(record.Delivering);
				Services->Draw(handle, *record.Renderer, *record.Frame, record.State, argument);
			};
			callbacks.CanRelease = [this, handle](auto task) {
				return CanReleasePayload(handle, task);
			};
			callbacks.Release = [this, handle](auto task) {
				static_cast<void>(Payload(handle, task));
				DestroyPayload(handle.Index);
			};

			return callbacks;
		}

		void RequireCanRelease(ScreenHandle handle) const {
			const auto& record = Record(handle);
			RequirePayloadIdle(record);
			if (record.Task.Owner != nullptr && (!Driver || !Driver->Contains(record.Task))) {
				throw std::runtime_error("Screen release lost its actual containing task owner");
			}
		}

		void Release(ScreenHandle handle) {
			RequireCanRelease(handle);
			auto& record = Record(handle);
			if (record.Task.Owner != nullptr) {
				// Native23a770 erases the wrapper pair and stores status2; the actual +130 traversal deletes the payload later.
				Driver->SetStatus(record.Task, TaskStatus::Retire);
				RemoveVisible(handle.Index);
			} else {
				DestroyPayload(handle.Index);
			}
		}

		bool CanClose() const noexcept {
			try {
				RequireThread();
				if (Closed) {
					return true;
				}
				if (Operating) {
					return false;
				}
				for (const auto& record : Records) {
					if (!record.PayloadActive) {
						continue;
					}
					RequirePayloadIdle(record);
					if (record.Task.Owner != nullptr && (!Driver || !Driver->CanExtract(record.Task))) {
						return false;
					}
				}

				return true;
			} catch (...) {
				return false;
			}
		}

		void Close() {
			RequireThread();
			if (Closed) {
				return;
			}
			if (!CanClose()) {
				throw std::runtime_error("Screen registry retirement requires its own quiescent target/manager/task registrations");
			}
			const ScreenRegistryOperation operation(Operating);
			const bool has_tasks = std::any_of(Records.begin(), Records.end(), [](const auto& record) {
				return record.PayloadActive && record.Task.Owner != nullptr;
			});
			if (has_tasks) {
				std::vector<ParallelTaskHandle> order(Driver->Count());
				static_cast<void>(Driver->WriteOrder(order));
				for (const auto handle : order) {
					for (auto& record : Records) {
						if (record.PayloadActive && record.Task == handle) {
							auto task = Driver->Extract(handle);
							task.Release();
							break;
						}
					}
				}
			} else {
				for (std::uint32_t index = 0; index < Records.size(); ++index) {
					if (Records[index].PayloadActive) {
						DestroyPayload(index);
					}
				}
			}
			Driver = nullptr;
			Services.reset();
			Closed = true;
		}
	};

	ScreenRegistry::ScreenRegistry(LayerTree& tree, TargetFactory factory, ScreenRegistryLimits limits)
	    : m_Implementation(std::make_unique<Implementation>(tree, std::move(factory), limits)) {
	}

	ScreenRegistry::~ScreenRegistry() {
		try {
			m_Implementation->Close();
		} catch (...) {
			// A remaining concrete task still borrows this registry's records; destruction cannot leave it dangling.
			std::terminate();
		}
	}

	void ScreenRegistry::ConfigureDriver(ScreenDriverServices services) {
		auto& state = *m_Implementation;
		state.RequireOpen();
		const ScreenRegistryOperation operation(state.Operating);
		if (state.Services || state.EverCreated || !services.ResolveTasks || !services.Layers.State || !services.Layers.Update || !services.Layers.Draw || !services.Layers.PrepareDraw || !services.Draw) {
			throw std::runtime_error("Screen driver requires one configuration before creation and all concrete +130 Layer/target services");
		}

		state.Services.emplace(std::move(services));
	}

	ScreenHandle ScreenRegistry::Create(ScreenDescription description) {
		auto& state = *m_Implementation;
		state.RequireOpen();
		const ScreenRegistryOperation operation(state.Operating);
		if (description.Width == 0 || description.Height == 0 || description.Width > state.Limits.MaxDimension || description.Height > state.Limits.MaxDimension) {
			throw std::runtime_error("screen target dimensions exceed configured limits");
		}
		const auto pixels = static_cast<std::uint64_t>(description.Width) * description.Height;
		if (pixels > state.Limits.MaxPixels - state.PixelCount) {
			throw std::runtime_error("screen target aggregate pixel limit exceeded");
		}

		for (std::uint32_t index = 0; index < state.Records.size(); ++index) {
			auto& record = state.Records[index];
			if (record.PayloadActive || record.Generation == std::numeric_limits<std::uint64_t>::max()) {
				continue;
			}
			ParallelTaskSet* driver = nullptr;
			if (state.Services) {
				driver = &state.Services->ResolveTasks();
				if (driver->Status() != TaskStatus::Active || (state.Driver && state.Driver != driver)) {
					throw std::runtime_error("Screen creation requires the same actual active SQDriver +130 group");
				}
			}

			// Native23acf0 constructs its reset-transform manager before the virtual-screen target.
			const auto layer_root = state.Tree.CreateScreenRoot();
			try {
				auto target = state.Factory(description);
				if (!target.Renderer || target.Width != description.Width || target.Height != description.Height) {
					throw std::runtime_error("screen factory did not return the requested actual render target");
				}
				record.State = {};
				record.State.Description = description;
				record.State.LayerRoot = layer_root;
				record.Renderer = std::move(target.Renderer);
				record.PayloadActive = true;
				state.PixelCount += pixels;
				if (driver) {
					record.Frame = std::make_unique<LayerManagerFrame>(state.Tree, layer_root, state.Services->Layers);
					auto callbacks = state.Callbacks(state.Handle(index));
					record.Task = description.InsertBefore ? driver->Prepend(std::move(callbacks)) : driver->Append(std::move(callbacks));
					state.Driver = driver;
				}
			} catch (...) {
				const auto failure = std::current_exception();
				if (record.PayloadActive) {
					state.DestroyPayload(index);
				} else {
					state.Tree.ReleaseScreenRoot(layer_root);
				}
				std::rethrow_exception(failure);
			}
			record.Active = true;
			if (description.InsertBefore) {
				state.Order.insert(state.Order.begin(), index);
			} else {
				state.Order.push_back(index);
			}
			state.EverCreated = true;

			return state.Handle(index);
		}

		throw std::runtime_error("screen registry capacity includes pending native task retirement");
	}

	bool ScreenRegistry::Contains(ScreenHandle screen) const {
		m_Implementation->RequireThread();

		return m_Implementation->Contains(screen);
	}

	ScreenState ScreenRegistry::State(ScreenHandle screen) const {
		return m_Implementation->Record(screen).State;
	}

	ScreenBounds ScreenRegistry::Bounds(ScreenHandle screen) const {
		const auto& description = m_Implementation->Record(screen).State.Description;
		ScreenBounds bounds;
		const auto width = static_cast<float>(description.Width);
		const auto height = static_cast<float>(description.Height);
		bounds.Left = -width * 0.5f;
		bounds.Top = -height * 0.5f;
		bounds.Right = width * 0.5f;
		bounds.Bottom = height * 0.5f;
		bounds.Width = bounds.Right - bounds.Left;
		bounds.Height = bounds.Bottom - bounds.Top;

		return bounds;
	}

	std::size_t ScreenRegistry::Count() const {
		m_Implementation->RequireThread();

		return m_Implementation->Order.size();
	}

	std::size_t ScreenRegistry::WriteOrder(std::span<ScreenHandle> output) const {
		const auto& state = *m_Implementation;
		state.RequireOpen();
		if (output.size() < state.Order.size()) {
			throw std::runtime_error("screen order output is too small");
		}
		for (std::size_t index = 0; index < state.Order.size(); ++index) {
			output[index] = state.Handle(state.Order[index]);
		}

		return state.Order.size();
	}

	render::SpritePassRenderer& ScreenRegistry::Renderer(ScreenHandle screen) const {
		return *m_Implementation->Record(screen).Renderer;
	}

	void ScreenRegistry::SetVisible(ScreenHandle screen, bool visible) {
		auto& state = *m_Implementation;
		auto& record = state.Record(screen);
		if (record.Task.Owner != nullptr) {
			state.Driver->SetDrawPaused(record.Task, !visible);
		}
		record.State.Visible = visible;
	}

	void ScreenRegistry::SetBackgroundColor(ScreenHandle screen, std::array<std::int32_t, 4> color) {
		m_Implementation->Record(screen).State.BackgroundColor = color;
	}

	void ScreenRegistry::RequireCanRelease(ScreenHandle screen) const {
		m_Implementation->RequireCanRelease(screen);
	}

	void ScreenRegistry::Release(ScreenHandle screen) {
		auto& state = *m_Implementation;
		state.RequireOpen();
		const ScreenRegistryOperation operation(state.Operating);
		state.Release(screen);
	}

	bool ScreenRegistry::CanClose() const noexcept {
		return m_Implementation->CanClose();
	}

	void ScreenRegistry::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::scene
