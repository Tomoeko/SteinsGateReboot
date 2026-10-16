#include "sgre/script/text_render_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/text/encoding.hpp"
#include "sgre/text/render_plan.hpp"

#include <algorithm>
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
#include <vector>

namespace sgre::script {

	static constexpr std::size_t kMaximumTextRenderInstances = 65536;
	static constexpr std::size_t kMethodPayloadBytes = 8;

	static SquirrelValue CaptureTextRenderMethod(SquirrelVm& vm, std::uint32_t method_id) {
		std::array<std::uint8_t, kMethodPayloadBytes> payload{1, 0, 0, 0};
		for (std::size_t byte = 0; byte < 4; ++byte) {
			payload[byte + 4] = static_cast<std::uint8_t>(method_id >> (byte * 8));
		}

		// Authored callback identity replaces the original pointer payload without executing reference machine code.
		return vm.CreateUserData(payload);
	}

	static void RequireTextRenderMethod(SquirrelVm& vm, std::uint32_t method_id) {
		const auto metadata = vm.NativeStackValue(-1);
		if (vm.Type(metadata) != SquirrelValueType::UserData) {
			throw std::runtime_error("TextRender method capture is not actual userdata");
		}

		const auto payload = vm.UserDataBytes(metadata);
		if (payload.size() != kMethodPayloadBytes || payload[0] != 1 || payload[1] != 0 || payload[2] != 0 || payload[3] != 0) {
			throw std::runtime_error("TextRender method userdata has invalid initialized metadata");
		}

		const auto observed = std::uint32_t(payload[4]) | (std::uint32_t(payload[5]) << 8) | (std::uint32_t(payload[6]) << 16) | (std::uint32_t(payload[7]) << 24);
		if (observed != method_id) {
			throw std::runtime_error("TextRender method userdata belongs to a different authored operation");
		}
	}

	static SquirrelValue TextRenderArgument(SquirrelVm& vm, std::size_t index) {
		// Native Sqrat helpers read a fixed allocated cell, including inactive cells beyond logical top.
		return vm.NativeAllocatedStackValue(static_cast<std::int32_t>(index + 1));
	}

	static float TextRenderNumber(SquirrelVm& vm, std::size_t index) {
		const auto argument = TextRenderArgument(vm, index);
		if (const auto* value = std::get_if<float>(&argument)) {
			return *value;
		}
		if (const auto* value = std::get_if<std::int32_t>(&argument)) {
			return static_cast<float>(*value);
		}
		if (const auto* value = std::get_if<bool>(&argument)) {
			return *value ? 1.0f : 0.0f;
		}

		return 0;
	}

	static std::int32_t TextRenderInteger(SquirrelVm& vm, std::size_t index) {
		return TryNativeInteger(TextRenderArgument(vm, index)).value_or(0);
	}

	static bool TextRenderBoolean(SquirrelVm& vm, std::size_t index) {
		const auto argument = TextRenderArgument(vm, index);
		if (std::holds_alternative<std::monostate>(argument)) {
			return false;
		}
		if (const auto* value = std::get_if<bool>(&argument)) {
			return *value;
		}
		if (const auto* value = std::get_if<std::int32_t>(&argument)) {
			return *value != 0;
		}
		if (const auto* value = std::get_if<float>(&argument)) {
			return *value != 0;
		}

		return true;
	}

	enum class TextPropertyTarget { Options,
		Defaults,
		Font };

	enum class TextResetTarget { Font,
		FontCache,
		Style,
		Empty };

	struct PreparedTextRun {
		const text::OwnedTextGlyphRun* Run = nullptr;
		text::TextCursor Offset;
		text::TextFontFamily Family = text::TextFontFamily::Normal;
		text::TextFontSelectionKey Key;
		ScriptTextFontInfo* Font = nullptr;
		std::string Face;
	};

	struct PreparedTextElement {
		const text::TextContentElement* Element = nullptr;
		// First run is normal; annotation runs retain the parent's reveal/style and actual ruby provider.
		std::vector<PreparedTextRun> Runs;
	};

	struct TextRenderTask {
		TextRenderTask(scene::MutableTextureBackend& backend, bool filtering)
		    : Context(backend, filtering) {
		}

		~TextRenderTask() {
			try {
				Close();
			} catch (...) {
			}
		}

		text::TextRenderContext Context;
		SquirrelHostRoot CachedSelf;
		std::unique_ptr<ScriptTextFontOwner> Fonts;
		bool Closing = false;
		bool SelectingFont = false;
		std::size_t PropertyDepth = 0;
		bool DeferredClose = false;
		bool GlyphOperation = false;
		bool GlyphsPrepared = false;
		std::vector<PreparedTextElement> DrawOrder;
		std::vector<text::CachedVectorGlyphDraw> GlyphScratch;
		std::vector<text::VectorGlyphDrawRectangle> EdgeDraws;
		std::vector<text::VectorGlyphDrawRectangle> OrdinaryDraws;

		void InvalidateGlyphs() {
			if (GlyphOperation) {
				throw std::runtime_error("TextRender cannot mutate content/fonts during actual glyph preparation or drawing");
			}
			GlyphsPrepared = false;
			DrawOrder.clear();
		}

		void Close() {
			if (Closing) {
				return;
			}

			if (SelectingFont || PropertyDepth != 0) {
				// A script callback may retire its Layer. Active option/font calls enclose its actual context until unwind.
				DeferredClose = true;

				return;
			}

			Closing = true;
			std::exception_ptr failure;
			try {
				if (Fonts) {
					Fonts->Close();
				}
			} catch (...) {
				failure = std::current_exception();
			}

			// Original 039c40 closes normal/ruby FontInfo owners before cached self and the base atlas.
			DrawOrder.clear();
			GlyphsPrepared = false;
			Fonts.reset();
			CachedSelf = {};
			try {
				Context.Atlas().Close();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	struct TextRenderBindingRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<TextRenderTask> Task;
		std::unique_ptr<scene::LayerScaleView> Scale;
		ScriptLayerBindings::BorrowedViewRegistration View;
		bool Detached = false;
		bool Releasing = false;
	};

	class ScriptTextRenderBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, scene::MutableTextureBackend& backend)
		    : Vm(vm), Objects(objects), Layers(layers), Tree(tree), Backend(backend) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		scene::LayerTree& Tree;
		scene::MutableTextureBackend& Backend;
		scene::LayerCameraDrawScope* Renderer = nullptr;
		VectorGlyphDrawing GlyphDrawing;
		ScriptPsbBindings* Psb = nullptr;
		ScriptVectorFontBindings* Vectors = nullptr;
		scene::BitmapFontAssetCatalog* Fonts = nullptr;
		scene::TextureCache* Textures = nullptr;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<TextRenderBindingRecord>> Instances;

		TextRenderBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("TextRender callback requires its actual native instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance) || found->second->Releasing) {
				throw std::runtime_error("TextRender instance has no live native wrapper");
			}

			return *found->second;
		}

		static std::shared_ptr<TextRenderTask> Task(const TextRenderBindingRecord& record) {
			if (record.Detached) {
				return {};
			}

			auto task = record.Task.lock();
			if (!task || task->Closing) {
				throw std::runtime_error("TextRender task is retiring before its parent detaches the borrowed wrapper");
			}

			return task;
		}

		TextFontLookupResult SelectFont(const SquirrelValue& instance, text::TextFontFamily family, text::TextFontSelectionKey key, std::string_view face_name) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task || !task->Fonts) {
				throw std::runtime_error("TextRender font lookup requires its actual live task and configured provider services");
			}
			if (task->SelectingFont) {
				throw std::runtime_error("TextRender cannot reenter its active findFont selection");
			}

			task->SelectingFont = true;
			TextFontLookupResult result;
			std::exception_ptr failure;
			try {
				result = task->Fonts->Select(family, key, face_name, task->Context.Atlas().State().Filtering);
			} catch (...) {
				failure = std::current_exception();
			}

			task->SelectingFont = false;
			if (task->DeferredClose) {
				try {
					task->Close();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}
			if (failure) {
				std::rethrow_exception(failure);
			}
			if (record->Detached || record->Releasing || task->Closing || record->Scale->TaskState().Status == scene::TaskStatus::Retire) {
				throw std::runtime_error("TextRender task retired during its actual findFont callback");
			}

			return result;
		}

		text::TextFontResetCallbacks FontCallbacks(const SquirrelValue& instance, const std::shared_ptr<TextRenderTask>& task) {
			text::TextFontResetCallbacks callbacks;
			const auto select = [this, instance, task](text::TextFontFamily family) {
				const auto request = text::PrepareTextFontSelectionRequest(task->Context.Formatting(), family, task->Context.Atlas().State().Filtering);
				const auto face = task->Context.FaceNames().Utf8Name(request.Key.FaceIndex);
				static_cast<void>(SelectFont(instance, family, request.Key, face));
			};
			callbacks.SelectNormal = [select] {
				select(text::TextFontFamily::Normal);
			};
			callbacks.SelectRuby = [select] {
				select(text::TextFontFamily::Ruby);
			};
			callbacks.MeasureNormal = [task](std::uint16_t unit) {
				if (!task->Fonts) {
					throw std::runtime_error("TextRender measurement requires its actual configured font owner");
				}

				return task->Fonts->MeasureCurrentNormalCodeUnit(unit);
			};

			return callbacks;
		}

		void FinishPropertyOperation(const std::shared_ptr<TextRenderBindingRecord>& record, const std::shared_ptr<TextRenderTask>& task, std::exception_ptr failure) {
			--task->PropertyDepth;
			if (task->DeferredClose) {
				try {
					task->Close();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}
			if (failure) {
				std::rethrow_exception(failure);
			}
			if (record->Detached || record->Releasing || task->Closing || record->Scale->TaskState().Status == scene::TaskStatus::Retire) {
				throw std::runtime_error("TextRender task retired during its actual property callback");
			}
		}

		void Reset(const SquirrelValue& instance, TextResetTarget target) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task) {
				return;
			}

			if (task->GlyphOperation) {
				throw std::runtime_error("TextRender cannot reset during actual glyph preparation or drawing");
			}
			if (target == TextResetTarget::FontCache || target == TextResetTarget::Empty) {
				task->InvalidateGlyphs();
			}
			++task->PropertyDepth;
			std::exception_ptr failure;
			try {
				if (target == TextResetTarget::FontCache) {
					if (task->Fonts) {
						task->Fonts->Clear();
					}
				} else if (target == TextResetTarget::Font) {
					task->Context.ResetFont(FontCallbacks(instance, task));
				} else if (target == TextResetTarget::Style) {
					task->Context.ResetStyle();
				} else {
					task->Context.ClearEmpty(FontCallbacks(instance, task));
				}
			} catch (...) {
				failure = std::current_exception();
			}

			FinishPropertyOperation(record, task, failure);
		}

		void Finalize(const SquirrelValue& instance) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task) {
				throw std::runtime_error("native TextRender done requires its live task");
			}

			task->InvalidateGlyphs();
			++task->PropertyDepth;
			std::exception_ptr failure;
			try {
				task->Context.Finalize();
				if (GlyphDrawing && task->Context.Content().ElementCount() != 0) {
					task->GlyphOperation = true;
					PrepareGlyphs(instance, task);
					task->GlyphsPrepared = true;
				}
			} catch (...) {
				failure = std::current_exception();
				task->DrawOrder.clear();
			}
			task->GlyphOperation = false;
			FinishPropertyOperation(record, task, failure);
		}

		void PrepareGlyphs(const SquirrelValue& instance, const std::shared_ptr<TextRenderTask>& task) {
			std::vector<const text::TextContentElement*> elements;
			for (const auto& line : task->Context.Content().Lines()) {
				for (const auto& element : line.Elements) {
					elements.push_back(&element);
				}
			}
			std::size_t max_run = 0;
			std::size_t rectangles = 0;
			task->DrawOrder.reserve(elements.size());
			for (const auto id : task->Context.Content().Plan().RenderOrder) {
				if (id >= elements.size()) {
					throw std::runtime_error("TextRender draw order escaped its actual owned finalized elements");
				}
				const auto& element = *elements[id];
				const auto pass_count = text::PrepareTextElementDrawPlan({}, element.Geometry.Position, element.Style).PassCount;
				PreparedTextElement prepared;
				prepared.Element = &element;
				prepared.Runs.reserve(1 + element.Ruby.size());
				PreparedTextRun normal;
				normal.Run = &element.Run;
				normal.Key = element.Font;
				normal.Face = task->Context.FaceNames().Utf8Name(element.Font.FaceIndex);
				prepared.Runs.push_back(std::move(normal));
				for (const auto& ruby : element.Ruby) {
					PreparedTextRun annotation;
					annotation.Run = &ruby.Run;
					annotation.Offset = ruby.Geometry.Position;
					annotation.Family = text::TextFontFamily::Ruby;
					annotation.Key = element.RubyFont;
					annotation.Key.RequestedSize = ruby.Geometry.Metrics.Height;
					annotation.Face = task->Context.FaceNames().Utf8Name(element.RubyFont.FaceIndex);
					prepared.Runs.push_back(std::move(annotation));
				}
				for (const auto& run : prepared.Runs) {
					if (run.Run->Glyphs.size() > 1024 * 1024 / 3 || run.Run->Glyphs.size() * pass_count > 1024 * 1024 - rectangles) {
						throw std::runtime_error("TextRender glyph rectangle preparation exceeds its bounded work");
					}
					rectangles += run.Run->Glyphs.size() * pass_count;
					max_run = std::max(max_run, run.Run->Glyphs.size());
				}
				task->DrawOrder.push_back(std::move(prepared));
			}
			// Native035e40 globally prepares stroked entries before unstroked entries, regardless of reveal count.
			for (const bool edge : {true, false}) {
				for (auto& prepared : task->DrawOrder) {
					const auto& element = *prepared.Element;
					if (edge && (!element.Style.EdgeEnabled || element.Style.EdgeWidth <= 0)) {
						continue;
					}
					for (auto& run : prepared.Runs) {
						const auto selected = SelectFont(instance, run.Family, run.Key, run.Face);
						run.Font = selected.Font;
						if (selected.Font == nullptr || selected.Font->Kind() == TextFontProviderKind::Unavailable) {
							continue;
						}
						if (selected.Font->Kind() != TextFontProviderKind::Vector) {
							throw std::runtime_error("TextRender bitmap glyph receiving requires its actual bitmap run draw producer");
						}
						selected.Font->PrepareVectorRun(task->Context.Atlas(), run.Run->Glyphs, edge ? element.Style.EdgeWidth : 0);
					}
				}
			}
			task->GlyphScratch.resize(max_run);
			task->EdgeDraws.resize(rectangles);
			task->OrdinaryDraws.resize(rectangles);
		}

		void AppendGlyphRun(const std::shared_ptr<TextRenderTask>& task, const PreparedTextRun& run, const text::TextGlyphDrawPass& pass, std::size_t& edge_count, std::size_t& ordinary_count) {
			if (run.Font == nullptr || run.Font->Kind() == TextFontProviderKind::Unavailable) {
				return;
			}
			const auto count = run.Font->PrepareVectorDrawRun(pass.Position, pass.ColorRgba, run.Run->Glyphs, task->GlyphScratch, pass.EdgeWidth);
			for (const auto& glyph : std::span(task->GlyphScratch).first(count)) {
				if (const auto rectangle = text::PrepareVectorGlyphDrawRectangle(glyph, task->Context.Atlas().Resource() != nullptr)) {
					auto& output = pass.Kind == text::TextGlyphPassKind::Edge ? task->EdgeDraws : task->OrdinaryDraws;
					auto& size = pass.Kind == text::TextGlyphPassKind::Edge ? edge_count : ordinary_count;
					if (size == output.size()) {
						throw std::runtime_error("TextRender glyph draw exceeded its actual prepared queue capacity");
					}
					output[size++] = *rectangle;
				}
			}
		}

		void Draw(const SquirrelValue& instance, const std::shared_ptr<TextRenderTask>& actual_task) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task || task != actual_task || task->Closing) {
				throw std::runtime_error("TextRender glyph draw lost its actual containing task");
			}
			if (task->Context.Content().ElementCount() == 0 || task->Context.State().ShownElementCount == 0) {
				return;
			}
			if (!GlyphDrawing || !task->GlyphsPrepared || task->GlyphOperation) {
				throw std::runtime_error("TextRender owned glyph-run drawing requires its actual prepared atlas and record receiver");
			}

			++task->PropertyDepth;
			task->GlyphOperation = true;
			std::exception_ptr failure;
			try {
				const auto& state = task->Context.State();
				const text::TextCursor origin{(state.OffsetPosition.X + state.Coordinate.X) + state.ScrollOffset.X, (state.OffsetPosition.Y + state.Coordinate.Y) + state.ScrollOffset.Y};
				std::size_t edge_count = 0;
				std::size_t ordinary_count = 0;
				const auto shown = std::min<std::size_t>(state.ShownElementCount, task->DrawOrder.size());
				for (std::size_t index = 0; index < shown; ++index) {
					const auto& prepared = task->DrawOrder[index];
					const auto& element = *prepared.Element;
					const auto& normal = prepared.Runs.front();
					const auto selected = SelectFont(instance, normal.Family, normal.Key, normal.Face);
					if (selected.Font != normal.Font) {
						throw std::runtime_error("TextRender glyph font cache changed after its actual done preparation");
					}
					const auto plan = text::PrepareTextElementDrawPlan(origin, element.Geometry.Position, element.Style);
					for (const auto& pass : std::span(plan.Passes).first(plan.PassCount)) {
						AppendGlyphRun(task, normal, pass, edge_count, ordinary_count);
					}
					for (const auto& pass : std::span(plan.Passes).first(plan.PassCount)) {
						for (std::size_t ruby = 1; ruby < prepared.Runs.size(); ++ruby) {
							const auto& run = prepared.Runs[ruby];
							const auto ruby_selected = SelectFont(instance, run.Family, run.Key, run.Face);
							if (ruby_selected.Font != run.Font) {
								throw std::runtime_error("TextRender ruby font cache changed after its actual done preparation");
							}
							AppendGlyphRun(task, run, text::PrepareTextRubyDrawPass(pass, run.Offset), edge_count, ordinary_count);
						}
					}
				}
				if (edge_count != 0) {
					GlyphDrawing(task->Context.Atlas(), std::span(task->EdgeDraws).first(edge_count), state.ClippingBounds);
				}
				if (ordinary_count != 0) {
					GlyphDrawing(task->Context.Atlas(), std::span(task->OrdinaryDraws).first(ordinary_count), state.ClippingBounds);
				}
			} catch (...) {
				failure = std::current_exception();
			}
			task->GlyphOperation = false;
			FinishPropertyOperation(record, task, failure);
		}

		bool Render(const SquirrelValue& instance, const std::string& input, const text::TextContentRequest& request) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task) {
				return false;
			}
			const auto nul = input.find('\0');
			const auto units = text::DecodeTextCodeUnits(std::string_view(input).substr(0, nul));
			task->InvalidateGlyphs();
			++task->PropertyDepth;
			std::exception_ptr failure;
			bool result = false;
			try {
				text::TextContentCallbacks callbacks;
				callbacks.Fonts = FontCallbacks(instance, task);
				callbacks.PrepareNormal = [task](std::uint16_t unit) {
					if (!task->Fonts) {
						throw std::runtime_error("Text.render requires the actual configured FontInfo owner");
					}
					return task->Fonts->PrepareCurrentNormalCodeUnit(unit);
				};
				callbacks.PrepareRuby = [task](std::u16string_view text) {
					if (!task->Fonts) {
						throw std::runtime_error("Text.render requires the actual configured ruby FontInfo owner");
					}
					return task->Fonts->PrepareCurrentRubyRun(text);
				};
				callbacks.MeasureRuby = [task](std::uint16_t unit) {
					if (!task->Fonts) {
						throw std::runtime_error("Text.render requires the actual configured ruby FontInfo owner");
					}
					return task->Fonts->MeasureCurrentRubyCodeUnit(unit).Width;
				};
				result = task->Context.Render(units, request, callbacks);
			} catch (...) {
				failure = std::current_exception();
			}
			FinishPropertyOperation(record, task, failure);
			return result;
		}

		void SetRenderSize(const SquirrelValue& instance, text::TextCursor size) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task) {
				return;
			}

			task->InvalidateGlyphs();
			++task->PropertyDepth;
			std::exception_ptr failure;
			try {
				task->Context.SetRenderSize(size, FontCallbacks(instance, task));
			} catch (...) {
				failure = std::current_exception();
			}

			FinishPropertyOperation(record, task, failure);
		}

		void ApplyProperties(const SquirrelValue& instance, const SquirrelValue& option, TextPropertyTarget target) {
			static_cast<void>(Record(instance));
			const auto record = Instances.at(std::get<SquirrelObjectHandle>(instance).Index);
			const auto task = Task(*record);
			if (!task) {
				return;
			}

			// Native041530/03f510 and their typed ObjectInfo helpers use the fixed bootstrap VM225ff0.
			const auto context = Vm.RootContext();
			text::TextPropertyReader reader;
			reader.ContainsRaw = [&](std::string_view key) {
				return Vm.ReadPropertyInContext(context, option, key, SquirrelPropertyReadKind::RawPresence).Present;
			};
			reader.ReadInteger = [&](std::string_view key) {
				return Vm.ReadPropertyInContext(context, option, key, SquirrelPropertyReadKind::Integer).Integer;
			};
			reader.ReadNumber = [&](std::string_view key) {
				return Vm.ReadPropertyInContext(context, option, key, SquirrelPropertyReadKind::Number).Number;
			};
			reader.ReadBoolean = [&](std::string_view key) {
				return Vm.ReadPropertyInContext(context, option, key, SquirrelPropertyReadKind::Boolean).Boolean;
			};
			reader.ReadString = [&](std::string_view key) {
				return Vm.ReadPropertyInContext(context, option, key, SquirrelPropertyReadKind::String).String;
			};
			if (task->GlyphOperation) {
				throw std::runtime_error("TextRender cannot apply properties during actual glyph preparation or drawing");
			}
			++task->PropertyDepth;
			std::exception_ptr failure;
			try {
				if (target == TextPropertyTarget::Defaults) {
					task->Context.ApplyDefaults(reader);
				} else if (target == TextPropertyTarget::Font) {
					task->Context.ApplyFontProperties(reader, FontCallbacks(instance, task));
				} else {
					task->Context.ApplyOptions(reader);
				}
			} catch (...) {
				failure = std::current_exception();
			}

			FinishPropertyOperation(record, task, failure);
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner) {
			if (arguments.size() < 2 || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("TextRender requires an actual Layer parent");
			}

			const auto& instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() >= kMaximumTextRenderInstances) {
				throw std::runtime_error("TextRender wrapper is initialized or its instance limit is exhausted");
			}

			auto record = std::make_shared<TextRenderBindingRecord>();
			record->Instance = Vm.BorrowObjectIdentity(instance);
			const auto weak_record = std::weak_ptr<TextRenderBindingRecord>(record);
			record->View = Layers.BorrowMotionView(
			    arguments[1], [weak_record](bool filtering) {
				    if (const auto view = weak_record.lock()) {
					    if (const auto task = Task(*view)) {
						    task->Context.Atlas().SetFiltering(filtering);
					    }
				    }
			    },
			    [weak_record] {
				    if (const auto view = weak_record.lock()) {
					    view->Detached = true;
					    if (view->Scale) {
						    view->Scale->DetachParent();
					    }
					    view->Task.reset();
				    }
			    });
			const auto filtering = Tree.State(Layers.Handle(arguments[1])).Smoothing;
			auto task = std::make_shared<TextRenderTask>(Backend, filtering);
			record->Task = task;
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
				task->CachedSelf = Vm.Retain(Vm.CreateWeakReference(instance));
				if (Psb) {
					task->Fonts = std::make_unique<ScriptTextFontOwner>(Vm, Objects, *Psb, *Vectors, *Fonts, *Textures, task->CachedSelf);
				}
				scene::LayerScaleTaskCallbacks callbacks;
				if (Psb) {
					// Only a task with the real configured FontInfo owner can publish content.
					callbacks.Draw = [binding_owner, task, instance](std::uint32_t) {
						const auto owner = binding_owner.lock();
						if (!owner) {
							throw std::runtime_error("TextRender drawing requires its live binding service");
						}
						owner->Draw(instance, task);
					};
					callbacks.DrawWithCamera = [binding_owner](const auto& camera, auto& children, auto argument) {
						const auto owner = binding_owner.lock();
						if (!owner || owner->Renderer == nullptr) {
							throw std::runtime_error("TextRender camera requires its actual graphics model/scissor scope");
						}
						owner->Renderer->Draw(scene::PrepareLayerCameraParameters(camera), children, argument);
					};
				}
				callbacks.Release = [task]() {
					task->Close();
				};
				record->Scale = std::make_unique<scene::LayerScaleView>(Layers.MotionTasks(arguments[1]), std::move(callbacks));
			} catch (...) {
				const auto failure = std::current_exception();
				if (record->Scale) {
					record->Scale->Retire();
				}
				record->View.Reset();
				Instances.erase(index);
				if (attached) {
					Objects.ConfigureNativeRelease(instance, {});
					Objects.ReleaseInstance(instance);
				}

				std::rethrow_exception(failure);
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			auto& record = Record(instance);
			record.Releasing = true;
			if (record.Scale) {
				record.Scale->Retire();
			}

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
	};

	ScriptTextRenderBindings::ScriptTextRenderBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, scene::MutableTextureBackend& backend)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, tree, backend)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		std::uint32_t next_method_id = 0;
		const auto method = [&](std::string name, bool returns_value, auto operation) {
			const auto method_id = ++next_method_id;
			const auto captured = name != "constructor";
			std::vector<SquirrelValue> captures;
			if (captured) {
				captures.push_back(CaptureTextRenderMethod(vm, method_id));
			}
			const auto callback = vm.CreateNativeClosureWithCaptures(
			    "TextRender." + name, [weak, returns_value, operation, captured, method_id](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
				    const auto owner = weak.lock();
				    if (!owner || arguments.empty()) {
					    throw std::runtime_error("TextRender callback owner has expired or lacks its receiver");
				    }

				    if (captured) {
					    RequireTextRenderMethod(active, method_id);
				    }

				    auto value = operation(*owner, arguments);

				    return returns_value ? SquirrelNativeResult::ReturnValue(std::move(value)) : SquirrelNativeResult::NoValues();
			    },
			    captures);
			vm.NewSlot(state.Class, name, callback);
		};
		method("constructor", false, [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak);

			return {};
		});
		method("render", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto receiver = arguments[0];
			text::TextContentRequest request;
			// Native038df0 converts fixed cells in reverse order before the potentially reentrant ToString.
			request.KeepPreviousTimes = TextRenderBoolean(owner.Vm, 5);
			request.TargetTime = TextRenderInteger(owner.Vm, 4);
			request.ElementDelay = TextRenderInteger(owner.Vm, 3);
			request.Indent = TextRenderInteger(owner.Vm, 2);
			const auto operand = TextRenderArgument(owner.Vm, 1);
			const auto input = owner.Vm.ToString(operand);
			return owner.Render(receiver, input, request);
		});
		method("setOption", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto receiver = arguments[0];
			const auto option = TextRenderArgument(owner.Vm, 1);
			owner.ApplyProperties(receiver, option, TextPropertyTarget::Options);

			return {};
		});
		method("setDefault", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto receiver = arguments[0];
			const auto defaults = TextRenderArgument(owner.Vm, 1);
			owner.ApplyProperties(receiver, defaults, TextPropertyTarget::Defaults);

			return {};
		});
		method("setFont", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto receiver = arguments[0];
			const auto font = TextRenderArgument(owner.Vm, 1);
			owner.ApplyProperties(receiver, font, TextPropertyTarget::Font);

			return {};
		});
		method("resetFont", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Reset(arguments[0], TextResetTarget::Font);

			return {};
		});
		method("resetStyle", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Reset(arguments[0], TextResetTarget::Style);

			return {};
		});
		method("clearFont", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Reset(arguments[0], TextResetTarget::FontCache);

			return {};
		});
		method("clear", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Reset(arguments[0], TextResetTarget::Empty);

			return {};
		});
		method("setRenderSize", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto receiver = arguments[0];
			// Native038bb0 reads the fixed height cell before width, then0425d0 checks detachment.
			const auto height = TextRenderNumber(owner.Vm, 2);
			const auto width = TextRenderNumber(owner.Vm, 1);
			owner.SetRenderSize(receiver, {width, height});

			return {};
		});
		method("setVertical", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetVertical(TextRenderBoolean(owner.Vm, 1));
			}

			return {};
		});
		method("setTimeScale", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetTimeScale(TextRenderNumber(owner.Vm, 1));
			}

			return {};
		});
		method("setFontScale", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetFontScale(TextRenderNumber(owner.Vm, 1));
			}

			return {};
		});
		method("setShowCount", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetShownElementCount(std::bit_cast<std::uint32_t>(TextRenderInteger(owner.Vm, 1)));
			}

			return {};
		});
		method("setCoord", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetCoordinate({TextRenderNumber(owner.Vm, 1), TextRenderNumber(owner.Vm, 2)});
			}

			return {};
		});
		method("setClipOffset", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetClipOffset({TextRenderNumber(owner.Vm, 1), TextRenderNumber(owner.Vm, 2)});
			}

			return {};
		});
		method("setOffsetPos", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (const auto task = Implementation::Task(owner.Record(arguments[0]))) {
				task->Context.SetOffsetPosition({TextRenderNumber(owner.Vm, 1), TextRenderNumber(owner.Vm, 2)});
			}

			return {};
		});
		method("getVertical", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.Formatting().Vertical : false;
		});
		method("getTimeScale", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.State().TimeScale : 1.0f;
		});
		method("getFontScale", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.State().FontScale : 1.0f;
		});
		method("getLeft", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.State().Coordinate.X : 0.0f;
		});
		method("getTop", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.State().Coordinate.Y : 0.0f;
		});
		method("getWidth", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.State().Size.X : 0.0f;
		});
		method("getHeight", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.State().Size.Y : 0.0f;
		});
		method("getRenderOver", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			return task ? task->Context.Content().Overflowed() : false;
		});
		method("getRenderDelay", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? task->Context.Content().RenderDelay(task->Context.State().TimeScale) : 0.0f;
		});
		method("calcShowCount", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto& record = owner.Record(arguments[0]);
			const auto time = TextRenderInteger(owner.Vm, 1);
			const auto task = Implementation::Task(record);

			return task ? std::bit_cast<std::int32_t>(task->Context.Content().CalculateShownElementCount(time, task->Context.State().TimeScale)) : std::int32_t{0};
		});
		method("getKeyWait", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			if (!task) {
				return {};
			}

			const auto boundaries = text::PrepareTextKeyWaits(task->Context.Content().RevealBoundaries(), task->Context.State().TimeScale);
			const auto result = owner.Vm.CreateArray();
			for (const auto& boundary : boundaries) {
				const auto entry = owner.Vm.CreateTable();
				owner.Vm.NewSlot(entry, std::string("pos"), std::bit_cast<std::int32_t>(boundary.FirstElement));
				owner.Vm.NewSlot(entry, std::string("time"), boundary.Time);
				owner.Vm.AppendArray(result, entry);
			}

			return result;
		});
		method("getRenderCurX", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? TryNativeInteger(task->Context.Content().RenderCursor().X).value_or(0) : std::int32_t{0};
		});
		method("getRenderCurY", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? TryNativeInteger(task->Context.Content().RenderCursor().Y).value_or(0) : std::int32_t{0};
		});
		method("getRenderCount", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));

			return task ? std::bit_cast<std::int32_t>(task->Context.Content().RenderedElementCount()) : std::int32_t{0};
		});
		method("getRenderLines", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			return task ? static_cast<std::int32_t>(task->Context.Content().Lines().size()) : std::int32_t{0};
		});
		method("getRenderBounds", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			if (!task) {
				return std::int32_t{0};
			}
			const auto bounds = task->Context.Content().Bounds();
			const auto table = owner.Vm.CreateTable();
			owner.Vm.NewSlot(table, std::string("left"), bounds.Left);
			owner.Vm.NewSlot(table, std::string("top"), bounds.Top);
			owner.Vm.NewSlot(table, std::string("right"), bounds.Right);
			owner.Vm.NewSlot(table, std::string("bottom"), bounds.Bottom);
			owner.Vm.NewSlot(table, std::string("width"), bounds.Right - bounds.Left);
			owner.Vm.NewSlot(table, std::string("height"), bounds.Bottom - bounds.Top);
			return table;
		});
		method("done", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Finalize(arguments[0]);

			return {};
		});
		method("setClipMode", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			if (!task) {
				throw std::runtime_error("native TextRender clip access requires its live task");
			}

			task->Context.SetClipMode(TextRenderInteger(owner.Vm, 1));

			return {};
		});
		method("getClipMode", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto task = Implementation::Task(owner.Record(arguments[0]));
			if (!task) {
				throw std::runtime_error("native TextRender clip access requires its live task");
			}

			return task->Context.State().ClipMode;
		});
		method("setVisible", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (Implementation::Task(record)) {
				record.Scale->SetVisible(TextRenderBoolean(owner.Vm, 1));
			}

			return {};
		});
		method("getVisible", true, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);

			return Implementation::Task(record) ? !record.Scale->TaskState().DrawPaused : false;
		});
		method("setOffset", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (Implementation::Task(record)) {
				record.Scale->SetOffset({TextRenderNumber(owner.Vm, 1), TextRenderNumber(owner.Vm, 2)});
			}

			return {};
		});
		method("setScale", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (Implementation::Task(record)) {
				record.Scale->SetScale({TextRenderNumber(owner.Vm, 1), TextRenderNumber(owner.Vm, 2)});
			}

			return {};
		});
		method("setPrimaryScale", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (Implementation::Task(record)) {
				record.Scale->SetPrimaryScale(TextRenderNumber(owner.Vm, 1));
			}

			return {};
		});
		method("setRot", false, [](Implementation& owner, const auto arguments) -> SquirrelValue {
			auto& record = owner.Record(arguments[0]);
			if (Implementation::Task(record)) {
				record.Scale->SetRotation(TextRenderNumber(owner.Vm, 1));
			}

			return {};
		});
		vm.NewSlot(vm.RootTable(), std::string("TextRender"), state.Class);
	}

	ScriptTextRenderBindings::~ScriptTextRenderBindings() = default;

	void ScriptTextRenderBindings::ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_scope) {
		auto& state = *m_Implementation;
		if (state.Renderer != nullptr || !state.Instances.empty() || state.Vm.IsClosed()) {
			throw std::runtime_error("TextRender frame services require one registration before wrapper construction");
		}
		state.Renderer = &actual_scope;
	}

	void ScriptTextRenderBindings::ConfigureGlyphDrawing(VectorGlyphDrawing draw) {
		auto& owner = *m_Implementation;
		if (!owner.Instances.empty() || owner.GlyphDrawing || !draw) {
			throw std::runtime_error("TextRender glyph receiving must be configured once before actual task construction");
		}
		owner.GlyphDrawing = std::move(draw);
	}

	void ScriptTextRenderBindings::ConfigureFontOwners(ScriptPsbBindings& psb, ScriptVectorFontBindings& vectors, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures) {
		auto& owner = *m_Implementation;
		if (!owner.Instances.empty() || owner.Psb) {
			throw std::runtime_error("TextRender font services must bind once before any actual task is constructed");
		}
		if (!fonts.UsesCache(textures) || owner.Vm.Type(psb.ObjectClass()) != SquirrelValueType::Class || owner.Vm.Type(vectors.VectorFontClass()) != SquirrelValueType::Class) {
			throw std::runtime_error("TextRender font services require actual VM classes and one texture catalog");
		}

		owner.Psb = &psb;
		owner.Vectors = &vectors;
		owner.Fonts = &fonts;
		owner.Textures = &textures;
	}

	TextFontLookupResult ScriptTextRenderBindings::SelectFont(const SquirrelValue& instance, text::TextFontFamily family, text::TextFontSelectionKey key, std::string_view face_name) {
		return m_Implementation->SelectFont(instance, family, key, face_name);
	}

	SquirrelValue ScriptTextRenderBindings::TextRenderClass() const {
		return m_Implementation->Class;
	}

	bool ScriptTextRenderBindings::IsDetached(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Detached;
	}

	const scene::LayerScaleView& ScriptTextRenderBindings::ScaleView(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Record(instance);
		if (!record.Scale) {
			throw std::runtime_error("TextRender wrapper has no actual shared scale owner");
		}

		return *record.Scale;
	}

	const text::TextRenderContext& ScriptTextRenderBindings::Context(const SquirrelValue& instance) const {
		const auto task = Implementation::Task(m_Implementation->Record(instance));
		if (!task) {
			throw std::runtime_error("detached TextRender wrapper has no actual text context");
		}

		return task->Context;
	}

	void ScriptTextRenderBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
