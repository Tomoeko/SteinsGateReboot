#include "sgre/script/indicator_bindings.hpp"

#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/script/font_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

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

	static constexpr std::size_t kMaxIndicatorInstances = 65536;

	struct BitmapIndicatorTask {
		explicit BitmapIndicatorTask(scene::TextureCache& cache, std::shared_ptr<const scene::LoadedBitmapFontAsset> asset) : Font(std::make_unique<scene::BitmapFontTextureOwner>(cache, asset)), Indicator(std::make_unique<scene::BitmapIndicator>(asset->Model)) {
		}

		BitmapIndicatorTask(scene::TextureCache& cache, std::shared_ptr<const scene::BitmapFontReceivingDocument> document) : Font(std::make_unique<scene::BitmapFontTextureOwner>(cache, std::move(document))), Indicator(std::make_unique<scene::BitmapIndicator>(Font->Asset().Model)) {
		}

		~BitmapIndicatorTask() {
			try {
				Close();
			} catch (...) {
			}
		}

		std::unique_ptr<scene::BitmapFontTextureOwner> Font;
		std::unique_ptr<scene::BitmapIndicator> Indicator;
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

			// Native MIndicator deletes its font before its prepared text elements.
			Font.reset();
			Indicator.reset();
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	struct IndicatorBindingRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<BitmapIndicatorTask> Task;
		scene::ParallelTaskSet* Group = nullptr;
		scene::ParallelTaskHandle Handle;
		std::optional<SquirrelHostRoot> PsbOwner;
		bool Detached = false;
		bool Releasing = false;
		ScriptLayerBindings::BorrowedViewRegistration View;
	};

	static float IndicatorNumber(std::span<const SquirrelValue> arguments, std::size_t index) {
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

	static std::int32_t IndicatorInteger(std::span<const SquirrelValue> arguments, std::size_t index) {
		return index < arguments.size() ? TryNativeInteger(arguments[index]).value_or(0) : 0;
	}

	static bool IndicatorBoolean(std::span<const SquirrelValue> arguments, std::size_t index) {
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

	class ScriptIndicatorBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptPsbBindings& psb, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, ScriptFontBindings* font_bindings) : Vm(vm), Objects(objects), Layers(layers), Tree(tree), Psb(psb), Fonts(fonts), Textures(textures), FontBindings(font_bindings) {
			if (!Fonts.UsesCache(Textures)) {
				throw std::runtime_error("Indicator requires one bitmap font catalog and texture cache");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		scene::LayerTree& Tree;
		ScriptPsbBindings& Psb;
		scene::BitmapFontAssetCatalog& Fonts;
		scene::TextureCache& Textures;
		ScriptFontBindings* FontBindings = nullptr;
		SquirrelValue ViewClass;
		SquirrelValue Class;
		SquirrelHostRoot ViewClassRoot;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<IndicatorBindingRecord>> Instances;

		IndicatorBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Indicator callback requires a native instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance) || found->second->Releasing) {
				throw std::runtime_error("Indicator instance has no live native view");
			}

			return *found->second;
		}

		static std::shared_ptr<BitmapIndicatorTask> Task(IndicatorBindingRecord& record) {
			if (record.Detached) {
				return {};
			}

			auto task = record.Task.lock();
			if (!task || task->Closing) {
				throw std::runtime_error("Indicator task is retiring before its parent view detaches");
			}

			return task;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner) {
			if (arguments.size() < 3 || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("Indicator requires an actual Layer and a recovered PSBObject or PSBFont bitmap provider");
			}

			SquirrelValue psb_owner;
			if (FontBindings && Vm.IsInstanceOf(arguments[2], FontBindings->FontClass())) {
				psb_owner = FontBindings->PsbOwner(arguments[2]);
			} else if (Vm.IsInstanceOf(arguments[2], Psb.ObjectClass())) {
				psb_owner = arguments[2];
			} else {
				throw std::runtime_error("Indicator requires a recovered PSBObject or PSBFont bitmap provider; vector Font providers remain unsupported");
			}

			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() >= kMaxIndicatorInstances) {
				throw std::runtime_error("Indicator native view is already initialized or its instance limit is exhausted");
			}

			auto record = std::make_shared<IndicatorBindingRecord>();
			record->Instance = Vm.BorrowObjectIdentity(instance);
			const auto weak_record = std::weak_ptr<IndicatorBindingRecord>(record);
			record->View = Layers.BorrowMotionView(arguments[1], [weak_record](bool smoothing) {
				if (const auto view = weak_record.lock()) {
					if (const auto task = Task(*view)) {
						const auto filter = smoothing ? 1 : 0;
						task->Font->SetFiltering(filter, filter);
					}
				}
			},
			    [weak_record] {
				    if (const auto view = weak_record.lock()) {
					    view->Detached = true;
					    view->Group = nullptr;
					    view->Task.reset();
				    }
			    });
			const auto document = Psb.DocumentAsset(psb_owner);
			std::shared_ptr<BitmapIndicatorTask> task;
			if (document.Font) {
				task = std::make_shared<BitmapIndicatorTask>(Textures, document.Font);
			} else {
				task = std::make_shared<BitmapIndicatorTask>(Textures, Fonts.Prepare(document.Document, document.Backing));
			}
			record->PsbOwner.emplace(Vm.Retain(psb_owner));
			record->Task = task;
			record->Group = &Layers.MotionTasks(arguments[1]).Group(scene::MotionTaskGroup::First);
			bool attached = false;
			try {
				Objects.AttachInstance(instance);
				attached = true;
				Instances.emplace(index, record);
				Objects.ConfigureNativeRelease(instance, [binding_owner](const SquirrelValue& receiver) {
					if (const auto owner = binding_owner.lock()) {
						owner->Release(receiver);
					}
				});
				// Empty animation state needs no update callback; bitmap draw submission is a separate open renderer boundary.
				scene::ParallelTaskCallbacks callbacks;
				callbacks.Release = [task](scene::ParallelTaskHandle) {
					task->Close();
				};
				scene::ParallelTaskState initial;
				initial.DrawPaused = true;
				record->Handle = record->Group->Append(std::move(callbacks), initial);
				const auto smoothing = Tree.State(Layers.Handle(arguments[1])).Smoothing;
				const auto filter = smoothing ? 1 : 0;
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
			record.Releasing = true;
			if (!record.Detached && record.Group->Contains(record.Handle)) {
				record.Group->SetStatus(record.Handle, scene::TaskStatus::Retire);
			}

			// The script owner retires now; the Layer still owns the marked task and physical font.
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

		void SetVisible(IndicatorBindingRecord& record, bool visible) {
			if (const auto task = Task(record)) {
				task->Indicator->Placement().SetVisible(visible);
				record.Group->SetDrawPaused(record.Handle, !visible);
			}
		}
	};

	ScriptIndicatorBindings::ScriptIndicatorBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptPsbBindings& psb, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, ScriptFontBindings* font_bindings) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, tree, psb, fonts, textures, font_bindings)) {
		auto& state = *m_Implementation;
		state.ViewClass = vm.CreateClass(objects.ObjectClass());
		state.ViewClassRoot = vm.Retain(state.ViewClass);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](const SquirrelValue& class_value, std::string name, bool returns_value, auto operation) {
			const auto callback = vm.CreateNativeClosure("Indicator." + name, [weak, returns_value, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Indicator callback owner has expired or lacks a receiver");
				}

				auto value = operation(*owner, arguments);

				return returns_value ? SquirrelNativeResult::ReturnValue(std::move(value)) : SquirrelNativeResult::NoValues();
			});
			vm.NewSlot(class_value, name, callback);
		};
		method(state.ViewClass, "constructor", false, [](Implementation&, const auto) -> SquirrelValue {
			throw std::runtime_error("can't create instance");
		});
		method(state.ViewClass, "setVisible", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.SetVisible(owner.Record(arguments[0]), IndicatorBoolean(arguments, 1));

			return {};
		});
		method(state.ViewClass, "getVisible", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Visible() : false;
		});
		method(state.ViewClass, "setCoord", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().SetCoordinate({IndicatorNumber(arguments, 1), IndicatorNumber(arguments, 2)});
			}

			return {};
		});
		method(state.ViewClass, "setLeft", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().SetEdge(scene::IndicatorEdge::Left, IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.ViewClass, "getLeft", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Bounds().Left : 0.0f;
		});
		method(state.ViewClass, "setTop", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().SetEdge(scene::IndicatorEdge::Top, IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.ViewClass, "getTop", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Bounds().Top : 0.0f;
		});
		method(state.ViewClass, "setRight", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().SetEdge(scene::IndicatorEdge::Right, IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.ViewClass, "getRight", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Bounds().Right : 0.0f;
		});
		method(state.ViewClass, "setBottom", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().SetEdge(scene::IndicatorEdge::Bottom, IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.ViewClass, "getBottom", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Bounds().Bottom : 0.0f;
		});
		method(state.ViewClass, "setSize", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().RequestSize({IndicatorNumber(arguments, 1), IndicatorNumber(arguments, 2)});
			}

			return {};
		});
		method(state.ViewClass, "setWidth", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().RequestSize({0, 0});
			}

			return {};
		});
		method(state.ViewClass, "getWidth", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Size().X : 0.0f;
		});
		method(state.ViewClass, "setHeight", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->Placement().RequestSize({0, 0});
			}

			return {};
		});
		method(state.ViewClass, "getHeight", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Placement().Size().Y : 0.0f;
		});
		state.Class = vm.CreateClass(state.ViewClass);
		state.ClassRoot = vm.Retain(state.Class);
		method(state.Class, "constructor", false, [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak);

			return {};
		});
		method(state.Class, "print", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto text = owner.Vm.ToString(arguments.size() > 1 ? arguments[1] : SquirrelValue{});
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			if (task) {
				task->Indicator->Print(text);
			}

			return {};
		});
		method(state.Class, "setFontScale", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetUniformFontScale(IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setFontScaleX", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetFontScaleX(IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setFontScaleY", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetFontScaleY(IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setFontColor", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetFontColor(static_cast<std::uint32_t>(IndicatorInteger(arguments, 1)));
			}

			return {};
		});
		method(state.Class, "setRecognizeTag", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetRecognizeTags(IndicatorBoolean(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setSpace", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetSpacing({IndicatorNumber(arguments, 1), IndicatorNumber(arguments, 2)});
			}

			return {};
		});
		method(state.Class, "setSpaceX", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetSpacingX(IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setSpaceY", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetSpacingY(IndicatorNumber(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setDrawAtIntegerCoord", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetDrawAtIntegerCoordinates(IndicatorBoolean(arguments, 1));
			}

			return {};
		});
		method(state.Class, "setOpacity", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetOpacity(static_cast<std::uint32_t>(IndicatorInteger(arguments, 1)));
			}

			return {};
		});
		method(state.Class, "setAlignment", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Indicator->SetAlignment(IndicatorInteger(arguments, 1));
			}

			return {};
		});
		method(state.Class, "getFontScale", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->FontScale().Width : 1.0f;
		});
		method(state.Class, "getFontScaleX", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->FontScale().Width : 1.0f;
		});
		method(state.Class, "getFontScaleY", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->FontScale().Height : 1.0f;
		});
		method(state.Class, "getFontMaxSizeX", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->MaximumFontSize().Width : 1.0f;
		});
		method(state.Class, "getFontMaxSizeY", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->MaximumFontSize().Height : 1.0f;
		});
		method(state.Class, "getRecognizeTag", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->RecognizeTags() : true;
		});
		method(state.Class, "getSpaceX", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Spacing().Width : 1.0f;
		});
		method(state.Class, "getSpaceY", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Spacing().Height : 1.0f;
		});
		method(state.Class, "getDrawAtIntegerCoord", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->DrawAtIntegerCoordinates() : true;
		});
		method(state.Class, "getOpacity", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? std::bit_cast<std::int32_t>(task->Indicator->Opacity()) : std::int32_t{255};
		});
		method(state.Class, "getAlignment", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Indicator->Alignment() : std::int32_t{0};
		});
		method(state.Class, "getFontColor", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? std::bit_cast<std::int32_t>(task->Indicator->FontColor()) : std::int32_t{-1};
		});
		vm.NewSlot(vm.RootTable(), std::string("LayerView"), state.ViewClass);
		vm.NewSlot(vm.RootTable(), std::string("Indicator"), state.Class);
	}

	ScriptIndicatorBindings::~ScriptIndicatorBindings() = default;

	SquirrelValue ScriptIndicatorBindings::LayerViewClass() const {
		return m_Implementation->ViewClass;
	}

	SquirrelValue ScriptIndicatorBindings::IndicatorClass() const {
		return m_Implementation->Class;
	}

	const scene::BitmapIndicator& ScriptIndicatorBindings::BitmapState(const SquirrelValue& instance) const {
		const auto task = Implementation::Task(m_Implementation->Record(instance));
		if (!task) {
			throw std::runtime_error("detached Indicator has no live bitmap task");
		}

		return *task->Indicator;
	}

	const scene::BitmapFontTextureOwner& ScriptIndicatorBindings::FontSources(const SquirrelValue& instance) const {
		const auto task = Implementation::Task(m_Implementation->Record(instance));
		if (!task) {
			throw std::runtime_error("detached Indicator has no live font sources");
		}

		return *task->Font;
	}

	void ScriptIndicatorBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
