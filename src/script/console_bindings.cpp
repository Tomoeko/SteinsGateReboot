#include "sgre/script/console_bindings.hpp"

#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/scene/indicator_placement.hpp"
#include "sgre/script/font_bindings.hpp"
#include "sgre/script/indicator_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"
#include "sgre/text/encoding.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	static float ConsoleNumber(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index >= arguments.size()) {
			return 0;
		}
		if (const auto* value = std::get_if<float>(&arguments[index])) {
			return *value;
		}
		if (const auto* value = std::get_if<std::int32_t>(&arguments[index])) {
			return static_cast<float>(*value);
		}
		if (const auto* value = std::get_if<bool>(&arguments[index])) {
			return *value ? 1.0f : 0.0f;
		}

		return 0;
	}

	static std::int32_t ConsoleInteger(std::span<const SquirrelValue> arguments, std::size_t index) {
		return index < arguments.size() ? TryNativeInteger(arguments[index]).value_or(0) : 0;
	}

	static bool ConsoleBoolean(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index >= arguments.size() || std::holds_alternative<std::monostate>(arguments[index])) {
			return false;
		}
		if (const auto* value = std::get_if<bool>(&arguments[index])) {
			return *value;
		}
		if (const auto* value = std::get_if<std::int32_t>(&arguments[index])) {
			return *value != 0;
		}
		if (const auto* value = std::get_if<float>(&arguments[index])) {
			return *value != 0;
		}

		return true;
	}

	static std::string ConsoleFirstUnit(const text::IndicatorTextElement& element) {
		const auto unit = element.CodeUnits.empty() ? std::uint16_t{0} : element.CodeUnits.front();
		const std::uint32_t scalar = unit >= 0xd800 && unit <= 0xdfff ? 0xfffd : unit;
		std::string result;
		if (scalar == 0) {
			return result;
		}
		if (scalar < 0x80) {
			result.push_back(static_cast<char>(scalar));
		} else if (scalar < 0x800) {
			result.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
			result.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
		} else {
			result.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
			result.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
			result.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
		}

		return result;
	}

	struct BitmapConsoleTask {
		BitmapConsoleTask(scene::TextureCache& cache, std::shared_ptr<const scene::LoadedBitmapFontAsset> asset, resource::GlyphSize extent) : Font(std::make_unique<scene::BitmapFontTextureOwner>(cache, asset)), Console(std::make_unique<scene::BitmapConsole>(asset->Model, extent)) {
			Placement.UpdateMeasuredSize({extent.Width, extent.Height});
		}

		BitmapConsoleTask(scene::TextureCache& cache, std::shared_ptr<const scene::BitmapFontReceivingDocument> document, resource::GlyphSize extent) : Font(std::make_unique<scene::BitmapFontTextureOwner>(cache, std::move(document))), Console(std::make_unique<scene::BitmapConsole>(Font->Asset().Model, extent)) {
			Placement.UpdateMeasuredSize({extent.Width, extent.Height});
		}

		~BitmapConsoleTask() {
			try {
				Close();
			} catch (...) {
			}
		}

		std::unique_ptr<scene::BitmapFontTextureOwner> Font;
		std::unique_ptr<scene::BitmapConsole> Console;
		scene::IndicatorPlacement Placement;
		bool Closing = false;

		void Close() {
			if (Closing) {
				return;
			}

			Closing = true;
			std::exception_ptr failure;
			try {
				Font->Close();
			} catch (...) {
				failure = std::current_exception();
			}
			Font.reset();
			Console.reset();
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	struct ConsoleBindingRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<BitmapConsoleTask> Task;
		scene::ParallelTaskSet* Group = nullptr;
		scene::ParallelTaskHandle Handle;
		std::optional<SquirrelHostRoot> PsbOwner;
		ScriptLayerBindings::BorrowedViewRegistration View;
		text::IndicatorTextElement LastPopped;
		bool Detached = false;
		bool Releasing = false;
		bool Operating = false;
	};

	class ScriptConsoleBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptPsbBindings& psb, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, ScriptConsoleServices services, ScriptFontBindings* font_bindings) : Vm(vm), Objects(objects), Layers(layers), Tree(tree), Psb(psb), Fonts(fonts), Textures(textures), Services(std::move(services)), FontBindings(font_bindings) {
			if (!Fonts.UsesCache(Textures) || !Services.ReadFrameDelta) {
				throw std::runtime_error("Console requires one actual bitmap cache and containing frame clock");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		scene::LayerTree& Tree;
		ScriptPsbBindings& Psb;
		scene::BitmapFontAssetCatalog& Fonts;
		scene::TextureCache& Textures;
		ScriptConsoleServices Services;
		ScriptFontBindings* FontBindings = nullptr;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<ConsoleBindingRecord>> Instances;

		ConsoleBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Console callback requires an actual native instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance) || found->second->Releasing) {
				throw std::runtime_error("Console instance has no live native view");
			}

			return *found->second;
		}

		static std::shared_ptr<BitmapConsoleTask> Task(ConsoleBindingRecord& record) {
			if (record.Detached) {
				return {};
			}

			auto task = record.Task.lock();
			if (!task || task->Closing) {
				throw std::runtime_error("Console task is retiring before its parent view detaches");
			}

			return task;
		}

		bool Filter(const SquirrelValue& receiver, const text::IndicatorTextElement& element) {
			if (!Vm.HasSlot(receiver, std::string("onFilter"))) {
				return false;
			}

			auto callback = Vm.ReadSlot(receiver, std::string("onFilter"));
			if (Vm.Type(callback) == SquirrelValueType::WeakReference) {
				callback = Vm.ResolveWeakReference(callback);
			}
			if (Vm.Type(callback) != SquirrelValueType::Closure && Vm.Type(callback) != SquirrelValueType::NativeClosure) {
				return false;
			}

			const std::array<SquirrelValue, 2> arguments{receiver, Vm.CreateString(ConsoleFirstUnit(element))};
			const auto result = Vm.Invoke(callback, arguments);
			if (result.State != SquirrelRunState::Completed || !std::holds_alternative<bool>(result.Value)) {
				throw std::runtime_error("Console onFilter requires a completed actual boolean result");
			}

			return std::get<bool>(result.Value);
		}

		void Update(ConsoleBindingRecord& record) {
			if (record.Operating) {
				throw std::runtime_error("Console task cannot reenter its containing update");
			}
			const auto task = Task(record);
			if (!task) {
				return;
			}

			record.Operating = true;
			try {
				Vm.DispatchBorrowedReceiver(record.Instance, [&](const auto& receiver) {
					task->Console->Update(Services.ReadFrameDelta(), [&](const auto& element) {
						return Filter(receiver, element);
					});
				});
				record.Group->SetStatus(record.Handle, task->Console->State().Running ? scene::TaskStatus::Active : scene::TaskStatus::Complete);
				record.Operating = false;
			} catch (...) {
				record.Operating = false;
				throw;
			}
		}

		void Draw(ConsoleBindingRecord& record, std::uint32_t argument) {
			const auto task = Task(record);
			if (!task) {
				return;
			}
			if (record.Operating || !Services.Draw) {
				throw std::runtime_error("Console drawing requires a real nonreentrant bitmap glyph receiver");
			}

			record.Operating = true;
			try {
				Services.Draw(*task->Console, task->Placement, *task->Font, argument);
				record.Operating = false;
			} catch (...) {
				record.Operating = false;
				throw;
			}
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner) {
			if (arguments.size() < 5 || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("Console requires an actual Layer, bitmap font provider, width and height");
			}

			SquirrelValue psb_owner;
			if (FontBindings && Vm.IsInstanceOf(arguments[2], FontBindings->FontClass())) {
				psb_owner = FontBindings->PsbOwner(arguments[2]);
			} else if (Vm.IsInstanceOf(arguments[2], Psb.ObjectClass())) {
				psb_owner = arguments[2];
			} else {
				throw std::runtime_error("Console requires an actual PSBObject or PSBFont; vector font providers remain unsupported");
			}

			const resource::GlyphSize extent{static_cast<float>(ConsoleInteger(arguments, 3)), static_cast<float>(ConsoleInteger(arguments, 4))};
			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() >= 65536) {
				throw std::runtime_error("Console native view is already initialized or its instance bound is exhausted");
			}

			auto record = std::make_shared<ConsoleBindingRecord>();
			record->Instance = Vm.BorrowObjectIdentity(instance);
			record->LastPopped.CodeUnits.push_back(0);
			const auto weak_record = std::weak_ptr<ConsoleBindingRecord>(record);
			record->View = Layers.BorrowMotionView(arguments[1], [weak_record](bool smoothing) {
				if (const auto view = weak_record.lock()) {
					if (const auto task = Task(*view)) {
						const auto filter = smoothing ? 1 : 0;
						task->Font->SetFiltering(filter, filter);
					}
				}
			}, [weak_record] {
				if (const auto view = weak_record.lock()) {
					view->Detached = true;
					view->Group = nullptr;
					view->Task.reset();
				}
			});
			const auto document = Psb.DocumentAsset(psb_owner);
			std::shared_ptr<BitmapConsoleTask> task;
			if (document.Font) {
				task = std::make_shared<BitmapConsoleTask>(Textures, document.Font, extent);
			} else {
				task = std::make_shared<BitmapConsoleTask>(Textures, Fonts.Prepare(document.Document, document.Backing), extent);
			}
			record->Task = task;
			record->PsbOwner.emplace(Vm.Retain(psb_owner));
			record->Group = &Layers.MotionTasks(arguments[1]).Group(scene::MotionTaskGroup::First);
			bool attached = false;
			try {
				Objects.AttachInstance(instance);
				attached = true;
				Instances.emplace(index, record);
				Objects.ConfigureNativeRelease(instance, [binding_owner](const auto& receiver) {
					if (const auto owner = binding_owner.lock()) {
						owner->Release(receiver);
					}
				});
				scene::ParallelTaskCallbacks callbacks;
				callbacks.Update = [binding_owner, record](auto) {
					const auto owner = binding_owner.lock();
					if (!owner) {
						throw std::runtime_error("Console task outlived its actual binding services");
					}
					owner->Update(*record);
				};
				callbacks.Draw = [binding_owner, record](auto, auto argument) {
					const auto owner = binding_owner.lock();
					if (!owner) {
						throw std::runtime_error("Console task outlived its actual binding services");
					}
					owner->Draw(*record, argument);
				};
				callbacks.CanRelease = [record](auto) {
					return !record->Operating;
				};
				callbacks.Release = [task](auto) {
					task->Close();
				};
				scene::ParallelTaskState initial;
				initial.DrawPaused = true;
				record->Handle = record->Group->Append(std::move(callbacks), initial);
				const auto filter = Tree.State(Layers.Handle(arguments[1])).Smoothing ? 1 : 0;
				task->Font->SetFiltering(filter, filter);
			} catch (...) {
				const auto failure = std::current_exception();
				if (record->Group->Contains(record->Handle)) {
					record->Group->SetStatus(record->Handle, scene::TaskStatus::Retire);
				}
				record->View.Reset();
				record->PsbOwner.reset();
				Instances.erase(index);
				if (attached) {
					try {
						Objects.ReleaseInstance(instance);
					} catch (...) {
					}
				}
				try {
					task->Close();
				} catch (...) {
				}
				std::rethrow_exception(failure);
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			auto& record = Record(instance);
			if (record.Operating) {
				throw std::runtime_error("Console native owner is borrowed by its actual filter/update/draw callback");
			}

			record.Releasing = true;
			if (!record.Detached && record.Group->Contains(record.Handle)) {
				record.Group->SetStatus(record.Handle, scene::TaskStatus::Retire);
			}
			record.PsbOwner.reset();
			record.View.Reset();
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void SetVisible(ConsoleBindingRecord& record, bool visible) {
			if (const auto task = Task(record)) {
				task->Placement.SetVisible(visible);
				record.Group->SetDrawPaused(record.Handle, !visible);
			}
		}

		void SetSize(ConsoleBindingRecord& record, resource::GlyphSize extent) {
			if (const auto task = Task(record)) {
				task->Console->SetExtent(extent);
				task->Placement.UpdateMeasuredSize({extent.Width, extent.Height});
			}
		}
	};

	ScriptConsoleBindings::ScriptConsoleBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptPsbBindings& psb, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, ScriptIndicatorBindings& views, ScriptConsoleServices services, ScriptFontBindings* font_bindings) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, tree, psb, fonts, textures, std::move(services), font_bindings)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(views.LayerViewClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, bool returns_value, auto operation) {
			const auto callback = vm.CreateNativeClosure("Console." + name, [weak, returns_value, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Console callback owner has expired or lacks a receiver");
				}

				auto value = operation(*owner, arguments);

				return returns_value ? SquirrelNativeResult::ReturnValue(std::move(value)) : SquirrelNativeResult::NoValues();
			});
			vm.NewSlot(state.Class, name, callback);
		};
		method("constructor", false, [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak);

			return {};
		});
		// Console uses the same genuine LayerView class, with its own concrete fixed-size view payload.
		method("setVisible", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.SetVisible(owner.Record(arguments[0]), ConsoleBoolean(arguments, 1));

			return {};
		});
		method("getVisible", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Placement.Visible() : false;
		});
		method("setCoord", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Placement.SetCoordinate({ConsoleNumber(arguments, 1), ConsoleNumber(arguments, 2)});
			}

			return {};
		});
		method("setLeft", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Placement.SetEdge(scene::IndicatorEdge::Left, ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("getLeft", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Placement.Bounds().Left : 0.0f;
		});
		method("setTop", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Placement.SetEdge(scene::IndicatorEdge::Top, ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("getTop", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Placement.Bounds().Top : 0.0f;
		});
		method("setRight", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Placement.SetEdge(scene::IndicatorEdge::Right, ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("getRight", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Placement.Bounds().Right : 0.0f;
		});
		method("setBottom", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Placement.SetEdge(scene::IndicatorEdge::Bottom, ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("getBottom", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Placement.Bounds().Bottom : 0.0f;
		});
		method("setSize", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.SetSize(owner.Record(arguments[0]), {ConsoleNumber(arguments, 1), ConsoleNumber(arguments, 2)});

			return {};
		});
		method("setWidth", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (const auto task = Implementation::Task(record)) {
				auto extent = task->Console->State().Extent;
				extent.Width = ConsoleNumber(arguments, 1);
				owner.SetSize(record, extent);
			}

			return {};
		});
		method("getWidth", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Extent.Width : 0.0f;
		});
		method("setHeight", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (const auto task = Implementation::Task(record)) {
				auto extent = task->Console->State().Extent;
				extent.Height = ConsoleNumber(arguments, 1);
				owner.SetSize(record, extent);
			}

			return {};
		});
		method("getHeight", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Extent.Height : 0.0f;
		});
		method("print", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			if (task) {
				if (arguments.size() < 2 || !TryStringText(arguments[1])) {
					throw std::runtime_error("Console.print requires an actual string");
				}
				task->Console->Print(StringText(arguments[1]));
				owner.Record(arguments[0]).Group->SetStatus(owner.Record(arguments[0]).Handle, scene::TaskStatus::Active);
			}

			return {};
		});
		method("clear", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (const auto task = Implementation::Task(record)) {
				task->Console->ClearDisplayed();
				record.Group->SetStatus(record.Handle, scene::TaskStatus::Active);
			}

			return {};
		});
		method("makeEmpty", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->MakeEmpty();
			}

			return {};
		});
		method("storePage", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->StorePage();
			}

			return {};
		});
		method("restorePage", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				const auto index = ConsoleInteger(arguments, 1);
				if (index < 0) {
					throw std::runtime_error("Console requested page is negative");
				}
				task->Console->RestorePage(static_cast<std::size_t>(index));
			}

			return {};
		});
		method("push", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (const auto task = Implementation::Task(record)) {
				if (arguments.size() != 2 || !TryStringText(arguments[1])) {
					throw std::runtime_error("invalid argument");
				}
				const auto& utf8 = StringText(arguments[1]);
				const auto units = text::DecodeTextCodeUnits(std::string_view(utf8).substr(0, utf8.find('\0')));
				if (units.empty()) {
					throw std::runtime_error("invalid argument");
				}
				// Native push replaces only the cached element's first UTF-16 unit, preserving its color and scale.
				record.LastPopped.CodeUnits.assign(1, units.front());
				task->Console->PushPending(record.LastPopped);
			}

			return {};
		});
		method("pop", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (const auto task = Implementation::Task(record)) {
				record.LastPopped = task->Console->PopPending();

				return owner.Vm.CreateString(ConsoleFirstUnit(record.LastPopped));
			}

			return {};
		});
		method("examine", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (Implementation::Task(record) && record.Group->State(record.Handle).Status == scene::TaskStatus::Active) {
				owner.Update(record);
			}

			return {};
		});
		method("setFontScale", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetFontScale(ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("setFontColor", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetFontColor(static_cast<std::uint32_t>(ConsoleInteger(arguments, 1)));
			}

			return {};
		});
		method("setFontSpeed", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetFontSpeed(ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("setScrollSpeed", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetScrollSpeed(ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("setProhibitionWidth", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetProhibitionWidth(ConsoleNumber(arguments, 1));
			}

			return {};
		});
		method("setRecognizeTag", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetRecognizeTags(ConsoleBoolean(arguments, 1));
			}

			return {};
		});
		method("setEraseLeftEndSpace", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetEraseLeftEndSpace(ConsoleBoolean(arguments, 1));
			}

			return {};
		});
		method("setWarnOverflow", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetWarnOverflow(ConsoleBoolean(arguments, 1));
			}

			return {};
		});
		method("setDrawAtIntegerCoord", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetDrawAtIntegerCoordinates(ConsoleBoolean(arguments, 1));
			}

			return {};
		});
		method("setOpacity", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetOpacity(static_cast<std::uint32_t>(ConsoleInteger(arguments, 1)));
			}

			return {};
		});
		method("setAlignment", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetAlignment(ConsoleInteger(arguments, 1));
			}

			return {};
		});
		method("setSpace", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Console->SetSpacing({ConsoleNumber(arguments, 1), ConsoleNumber(arguments, 2)});
			}

			return {};
		});
		method("setSpaceX", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				auto spacing = task->Console->State().Spacing;
				spacing.Width = ConsoleNumber(arguments, 1);
				task->Console->SetSpacing(spacing);
			}

			return {};
		});
		method("setSpaceY", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				auto spacing = task->Console->State().Spacing;
				spacing.Height = ConsoleNumber(arguments, 1);
				task->Console->SetSpacing(spacing);
			}

			return {};
		});
		method("getFontScale", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().FontScale : 1.0f;
		});
		method("getFontSpeed", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().FontSpeed : 1.0f;
		});
		method("getScrollSpeed", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().ScrollSpeed : 1.0f;
		});
		method("getProhibitionWidth", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().ProhibitionWidth : 1.0f;
		});
		method("getRecognizeTag", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().RecognizeTags : true;
		});
		method("getEraseLeftEndSpace", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().EraseLeftEndSpace : true;
		});
		method("getWarnOverflow", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().WarnOverflow : true;
		});
		method("getDrawAtIntegerCoord", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().DrawAtIntegerCoordinates : true;
		});
		method("getAlignment", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Alignment : std::int32_t{0};
		});
		method("getSpaceX", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Spacing.Width : 1.0f;
		});
		method("getSpaceY", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Spacing.Height : 1.0f;
		});
		method("getCursorX", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->AlignedCursor().Width : 1.0f;
		});
		method("getCursorY", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->AlignedCursor().Height : 1.0f;
		});
		method("getDrawnWidth", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Drawn.Width : 1.0f;
		});
		method("getDrawnHeight", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Drawn.Height : 1.0f;
		});
		method("getRunning", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->State().Running : false;
		});
		method("getEmpty", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Console->PendingCount() == 0 : true;
		});
		method("getFontColor", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? std::bit_cast<std::int32_t>(task->Console->State().FontColor) : std::int32_t{-1};
		});
		method("getOpacity", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? std::bit_cast<std::int32_t>(task->Console->State().Opacity) : std::int32_t{255};
		});
		method("getPageCount", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? static_cast<std::int32_t>(task->Console->PageCount()) : std::int32_t{0};
		});
		method("animateOpacity", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (Implementation::Task(owner.Record(arguments[0]))) {
				throw std::runtime_error("Console opacity animation requires its genuine recovered serial animation task");
			}

			return {};
		});
		vm.NewSlot(vm.RootTable(), std::string("Console"), state.Class);
		const auto constants = vm.CreateTable();
		vm.NewSlot(constants, std::string("ALIGNMENT_LEFT"), std::int32_t{0});
		vm.NewSlot(constants, std::string("ALIGNMENT_CENTER"), std::int32_t{1});
		vm.NewSlot(constants, std::string("ALIGNMENT_RIGHT"), std::int32_t{2});
		vm.NewSlot(vm.ConstantTable(), std::string("CONSOLE"), constants);
	}

	ScriptConsoleBindings::~ScriptConsoleBindings() = default;

	SquirrelValue ScriptConsoleBindings::ConsoleClass() const {
		return m_Implementation->Class;
	}

	const scene::BitmapConsole& ScriptConsoleBindings::BitmapState(const SquirrelValue& instance) const {
		const auto task = Implementation::Task(m_Implementation->Record(instance));
		if (!task) {
			throw std::runtime_error("detached Console has no live bitmap task");
		}

		return *task->Console;
	}

	void ScriptConsoleBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
