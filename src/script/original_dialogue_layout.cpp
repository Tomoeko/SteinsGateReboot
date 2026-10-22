#include "sgre/script/original_dialogue_layout.hpp"

#include "sgre/script/text_render_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>

namespace sgre::script {

	static float DialogueNumber(SquirrelVm& vm, const SquirrelValue& instance, const char* key) {
		const auto value = vm.GetSlot(instance, std::string(key));
		float number = 0;
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			number = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<float>(&value)) {
			number = *real;
		} else {
			throw std::runtime_error("dialogue layout requires original numeric field " + std::string(key));
		}
		if (!std::isfinite(number)) {
			throw std::runtime_error("dialogue layout received a nonfinite original field " + std::string(key));
		}

		return number;
	}

	static SquirrelValue InvokeDialogueMethod(SquirrelVm& vm, const SquirrelValue& instance, const char* method, std::span<const SquirrelValue> arguments) {
		const auto result = vm.InvokeInContext(vm.CurrentContext(), vm.GetSlot(instance, std::string(method)), arguments);
		if (result.State != SquirrelRunState::Completed) {
			throw std::runtime_error("dialogue layout requires its original synchronous " + std::string(method));
		}

		return result.Value;
	}

	class OriginalDialogueLayout::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptTextRenderBindings& text) : Vm(vm), Text(text) {
		}

		SquirrelVm& Vm;
		ScriptTextRenderBindings& Text;
		bool Installed = false;

		bool IsMainDialogue(const SquirrelValue& instance) {
			const auto root = Vm.RootTable();
			if (!Vm.HasSlot(root, std::string("EnvMessage")) || !Vm.HasSlot(instance, std::string("_owner"))) {
				return false;
			}
			auto owner = Vm.GetSlot(instance, std::string("_owner"));
			if (Vm.Type(owner) == SquirrelValueType::WeakReference) {
				owner = Vm.ResolveWeakReference(owner);
			}
			if (!Vm.IsInstanceOf(owner, Vm.GetSlot(root, std::string("EnvMessage")))) {
				return false;
			}

			// Original message.nut selects this semantic slot for dialogue; names and other text share the owner.
			const std::array<SquirrelValue, 2> arguments{owner, std::string("message")};
			const auto selected = InvokeDialogueMethod(Vm, owner, "getText", arguments);
			const auto selected_root = Vm.Retain(selected);
			if (Vm.Type(selected) != SquirrelValueType::Instance || !Vm.HasSlot(selected, std::string("info"))) {
				return false;
			}
			const auto info = Vm.GetSlot(selected, std::string("info"));
			const auto retained = Vm.Retain(info);
			if (Vm.Type(info) != SquirrelValueType::Instance || !Vm.HasSlot(info, std::string("render"))) {
				return false;
			}

			return Vm.GetSlot(info, std::string("render")) == instance;
		}

		void Apply(const SquirrelValue& instance) {
			const auto& content = Text.Context(instance);
			if (content.State().Vertical || content.Content().Lines().size() <= 3 || !IsMainDialogue(instance)) {
				return;
			}
			const auto original_scale = DialogueNumber(Vm, instance, "_scale");
			const auto width = DialogueNumber(Vm, instance, "_width") * original_scale;
			const auto height = DialogueNumber(Vm, instance, "_height") * original_scale;
			if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
				throw std::runtime_error("dialogue layout requires its original positive finite text box");
			}

			const auto ink = Text.DrawingBounds(instance);
			const auto ink_width = ink.Right - ink.Left;
			const auto ink_height = ink.Bottom - ink.Top;
			if (!std::isfinite(ink_width) || !std::isfinite(ink_height) || ink_width <= 0 || ink_height <= 0) {
				throw std::runtime_error("dialogue layout requires positive finite completed glyph bounds");
			}

			// Fit the entire completed block, not the revealed prefix, so letters never change size while appearing.
			const auto scale = std::min({1.0f, width / ink_width, height / ink_height});
			if (!std::isfinite(scale) || scale <= 0) {
				throw std::runtime_error("dialogue layout fit exceeds finite presentation coordinates");
			}
			const auto offset_x = ink.Left * scale;
			const auto offset_y = ink.Top * scale;
			const text::TextBounds clip{ink.Left, ink.Top, ink.Left + width / scale, ink.Top + height / scale};
			Vm.SetSlot(instance, std::string("_scalex"), scale);
			Vm.SetSlot(instance, std::string("_scaley"), scale);
			Vm.SetSlot(instance, std::string("_offx"), offset_x);
			Vm.SetSlot(instance, std::string("_offy"), offset_y);
			Text.SetPresentationClip(instance, clip);
			const std::array<SquirrelValue, 1> arguments{instance};
			static_cast<void>(InvokeDialogueMethod(Vm, instance, "_updatePosition", arguments));
		}
	};

	OriginalDialogueLayout::OriginalDialogueLayout(SquirrelVm& vm, ScriptTextRenderBindings& text) : m_Implementation(std::make_shared<Implementation>(vm, text)) {
	}

	OriginalDialogueLayout::~OriginalDialogueLayout() = default;

	void OriginalDialogueLayout::InstallAvailableHook() {
		auto& owner = *m_Implementation;
		auto& vm = owner.Vm;
		if (owner.Installed || vm.IsClosed() || !vm.HasSlot(vm.RootTable(), std::string("BasicRender"))) {
			return;
		}
		const auto type = vm.GetSlot(vm.RootTable(), std::string("BasicRender"));
		const auto original = vm.GetSlot(type, std::string("_updateFit"));
		if (vm.Type(type) != SquirrelValueType::Class || vm.Type(original) != SquirrelValueType::Closure) {
			throw std::runtime_error("dialogue layout requires the original BasicRender._updateFit closure");
		}

		const std::weak_ptr<Implementation> weak = m_Implementation;
		const auto hook = vm.CreateNativeClosureWithCaptures("dialogue.readableFit", [weak](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			const auto state = weak.lock();
			if (!state || arguments.size() < 2) {
				throw std::runtime_error("dialogue layout lost its original text service or receiver");
			}
			const auto original_arguments = arguments.first(arguments.size() - 1);
			const auto result = active.InvokeInContext(active.CurrentContext(), active.NativeStackValue(-1), original_arguments);
			if (result.State != SquirrelRunState::Completed) {
				throw std::runtime_error("dialogue layout requires the original synchronous fit completion");
			}
			state->Apply(original_arguments.front());

			return SquirrelNativeResult::NoValues();
		},
		    std::array<SquirrelValue, 1>{original});
		vm.NewSlot(type, std::string("_updateFit"), hook);
		owner.Installed = true;
	}
} // namespace sgre::script
