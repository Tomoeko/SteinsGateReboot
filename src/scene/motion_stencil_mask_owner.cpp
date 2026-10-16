#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {
	static constexpr std::uint32_t kStencilControlMask = 0x20000000;
	static constexpr std::size_t kMaximumStencilMaskNames = 65536;

	static const resource::PsbValue* StencilProperty(const resource::MotionLayer& layer, std::string_view name) {
		for (const auto& field : layer.Properties) {
			if (field.first == name) {
				return &field.second;
			}
		}

		return nullptr;
	}

	static float StencilNumber(const resource::PsbValue* value, std::string_view field) {
		if (value == nullptr || std::holds_alternative<std::monostate>(value->Data)) {
			return 0;
		}
		double number = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
			number = static_cast<double>(*integer);
		} else if (const auto* real = std::get_if<double>(&value->Data)) {
			number = *real;
		} else {
			throw std::runtime_error("motion stencil control requires numeric " + std::string(field));
		}

		if (!std::isfinite(number) || std::abs(number) > 1000000000) {
			throw std::runtime_error("motion stencil control requires bounded finite " + std::string(field));
		}

		return static_cast<float>(number);
	}

	static bool StencilBoolean(const resource::PsbValue* value, std::string_view field) {
		if (value != nullptr) {
			if (const auto* boolean = std::get_if<bool>(&value->Data)) {
				return *boolean;
			}
			if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
				return *integer != 0;
			}
			if (const auto* real = std::get_if<double>(&value->Data)) {
				// Native4ca6d0 compares the original float/double against zero; unordered values are true.
				return *real != 0;
			}
		}

		throw std::runtime_error("motion stencil control requires actual boolean/numeric " + std::string(field));
	}

	static std::uint32_t StencilWord(const resource::PsbValue* value, std::string_view field) {
		if (value == nullptr) {
			return 0;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value->Data); integer != nullptr && *integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::uint32_t>::max()) {
			return static_cast<std::uint32_t>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value->Data)) {
			return *boolean ? 1u : 0u;
		}
		if (const auto* real = std::get_if<double>(&value->Data); real != nullptr && std::isfinite(*real) && *real >= std::numeric_limits<std::int32_t>::min() && *real <= std::numeric_limits<std::int32_t>::max()) {
			return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
		}

		throw std::runtime_error("motion stencil requires bounded native word " + std::string(field));
	}

	static std::optional<std::uint32_t> StencilEasingIndex(const resource::PsbValue* value, const resource::MotionResource* actual_resource) {
		if (value == nullptr || std::holds_alternative<std::monostate>(value->Data)) {
			return {};
		}
		const auto index = StencilWord(value, "wcc");
		if (actual_resource == nullptr || index >= actual_resource->Easing.size()) {
			throw std::runtime_error("motion stencil wcc requires an index in its actual resource easing table");
		}

		return index;
	}

	MotionStencilMaskOwner::MotionStencilMaskOwner(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared) : MotionStencilMaskOwner(layer, prepared, nullptr) {
	}

	MotionStencilMaskOwner::MotionStencilMaskOwner(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const resource::MotionResource& resource) : MotionStencilMaskOwner(layer, prepared, &resource) {
	}

	MotionStencilMaskOwner::MotionStencilMaskOwner(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const resource::MotionResource* resource) : m_Resource(resource), m_Layer(layer), m_Prepared(prepared), m_MaskNames(StencilProperty(layer, "stencilCompositeMaskLayerList")) {
		if (layer.Type != 12 || prepared.Type != 12 || layer.Label != prepared.Label || layer.Frames.size() < 2 || layer.Frames.size() > 65536 || layer.Frames.size() != prepared.Frames.size() || prepared.MeshTransform != 0 || layer.Coordinate != prepared.CoordinatePlane || layer.InheritMask != prepared.InheritMask || StencilWord(StencilProperty(layer, "stencilType"), "stencilType") != prepared.StencilType) {
			throw std::runtime_error("motion stencil mask requires matching bounded kind12 preparation");
		}
		if (m_MaskNames != nullptr && !std::holds_alternative<std::monostate>(m_MaskNames->Data)) {
			const auto* names = std::get_if<resource::PsbArray>(&m_MaskNames->Data);
			if (names == nullptr || names->size() > kMaximumStencilMaskNames) {
				throw std::runtime_error("motion stencil mask names require a bounded actual array");
			}
			for (const auto& value : *names) {
				const auto* name = std::get_if<std::string>(&value.Data);
				if (name == nullptr || name->size() > kMaximumMotionCommandNameBytes) {
					throw std::runtime_error("motion stencil mask requires bounded actual layer names");
				}
			}
		}
		for (std::size_t index = 0; index < layer.Frames.size(); ++index) {
			const auto& raw = layer.Frames[index];
			const auto& frame = prepared.Frames[index];
			if (raw.Time != frame.Time || frame.Hidden != (raw.Type == 0) || frame.Tween != (raw.Type == 3) || (raw.Type != 0 && raw.Type != 2 && raw.Type != 3)) {
				throw std::runtime_error("motion stencil mask requires matching ordinary selected frame metadata");
			}
			if (!std::isfinite(raw.Time) || (index != 0 && raw.Time < layer.Frames[index - 1].Time) || (!frame.Hidden && StencilWord(raw.Content.Find("mask"), "mask") != frame.Attributes.Mask)) {
				throw std::runtime_error("motion stencil frame time/mask differs from actual preparation");
			}
			if (!frame.Hidden && (frame.Attributes.Mask & kStencilControlMask) != 0) {
				const auto* stc = raw.Content.Find("stc");
				if (stc == nullptr || !std::holds_alternative<resource::PsbObject>(stc->Data)) {
					throw std::runtime_error("motion stencil control bit requires its actual stc object");
				}
				static_cast<void>(StencilEasingIndex(stc->Find("wcc"), m_Resource));
				static_cast<void>(StencilBoolean(stc->Find("wrv"), "wrv"));
				static_cast<void>(StencilNumber(stc->Find("wrt"), "wrt"));
				static_cast<void>(StencilNumber(stc->Find("wsf"), "wsf"));
			}
		}
	}

	void MotionStencilMaskOwner::PrepareFrame(std::size_t slot, std::size_t frame_index) {
		if (slot >= m_Controls.size() || frame_index >= m_Layer.Frames.size()) {
			throw std::runtime_error("motion stencil decoded slot/frame exceeds actual preparation");
		}
		m_FrameIndices[slot] = frame_index;
		const auto& frame = m_Prepared.Frames[frame_index];
		if (frame.Hidden) {
			return;
		}
		auto& control = m_Controls[slot];
		control.Enabled = (frame.Attributes.Mask & kStencilControlMask) != 0;
		if (!control.Enabled) {
			// Native514410 releases the curve while retaining the other physical-slot scalars.
			control.EasingIndex.reset();
			return;
		}
		const auto& stc = *m_Layer.Frames[frame_index].Content.Find("stc");
		control.ReverseWrite = StencilBoolean(stc.Find("wrv"), "wrv");
		control.EasingIndex = StencilEasingIndex(stc.Find("wcc"), m_Resource);
		control.Threshold = StencilNumber(stc.Find("wrt"), "wrt");
		control.Softness = StencilNumber(stc.Find("wsf"), "wsf");
	}

	const MotionStencilFrameControl& MotionStencilMaskOwner::Control(std::size_t slot) const {
		if (slot >= m_Controls.size() || !m_FrameIndices[slot]) {
			throw std::runtime_error("motion stencil control slot has no genuine decoded frame");
		}

		return m_Controls[slot];
	}

	bool MotionStencilMaskOwner::Evaluate(const resource::MotionResource& resource, float position, std::size_t current_slot, bool force, bool frame_dirty) {
		m_SummaryPublished = false;
		if (m_Resource != nullptr && m_Resource != &resource) {
			throw std::runtime_error("motion stencil evaluation requires its actual retained resource easing owner");
		}
		if (current_slot >= m_Controls.size() || !m_FrameIndices[current_slot] || !m_FrameIndices[1 - current_slot] || *m_FrameIndices[current_slot] + 1 != *m_FrameIndices[1 - current_slot]) {
			throw std::runtime_error("motion stencil summary requires its actual adjacent decoded slots");
		}
		const auto index = *m_FrameIndices[current_slot];
		auto evaluation = m_Evaluation;
		auto summary = m_Summary;
		const bool changed = EvaluateCachedMotionLayer(m_Prepared, resource, position, index, force, frame_dirty, evaluation);
		const auto& current = m_Prepared.Frames[index];
		if (!current.Hidden && changed) {
			const auto& control = m_Controls[current_slot];
			summary.Enabled = control.Enabled;
			if (control.Enabled) {
				summary.ReverseWrite = control.ReverseWrite;
				float threshold = control.Threshold;
				float softness = control.Softness;
				if (current.Tween && !m_Prepared.Frames[index + 1].Hidden && std::abs(evaluation.Ratio) >= std::numeric_limits<float>::epsilon()) {
					const auto& following = m_Controls[1 - current_slot];
					const auto* easing = control.EasingIndex ? &resource.Easing.at(*control.EasingIndex) : nullptr;
					threshold = resource::InterpolateScalar(threshold, following.Threshold, evaluation.Ratio, easing);
					softness = resource::InterpolateScalar(softness, following.Softness, evaluation.Ratio, easing);
				}
				const auto denominator = softness * 1023.0f + 1.0f;
				summary.Scale = 1024.0f / denominator;
				summary.Offset = 1.0f - (summary.Scale + 1.0f) * threshold;
				if (!std::isfinite(summary.Scale) || !std::isfinite(summary.Offset)) {
					throw std::runtime_error("motion stencil summary exceeds its finite coefficient domain");
				}
			}
		}
		m_Evaluation = std::move(evaluation);
		m_Summary = summary;
		m_EvaluatedFrameIndex = index;
		m_EvaluatedPosition = position;
		m_SummaryPublished = true;

		return changed;
	}

	const MotionStencilMaskSummary& MotionStencilMaskOwner::Summary() const {
		if (!m_SummaryPublished) {
			throw std::runtime_error("motion stencil summary requires genuine initial numeric evaluation");
		}

		return m_Summary;
	}

	std::size_t MotionStencilMaskOwner::EvaluatedFrameIndex() const {
		static_cast<void>(Summary());

		return m_EvaluatedFrameIndex;
	}

	float MotionStencilMaskOwner::EvaluatedPosition() const {
		static_cast<void>(Summary());

		return m_EvaluatedPosition;
	}

	void MotionStencilMaskOwner::BindMaskLayers(MotionSelectedLayerNodes& selected) {
		if (m_Bound) {
			throw std::runtime_error("motion stencil mask list is exclusive to one live selected owner");
		}
		const auto nodes = selected.Nodes();
		bool genuine_owner = false;
		for (const auto& node : nodes) {
			genuine_owner = genuine_owner || (node.Raw == &m_Layer && node.Prepared == &m_Prepared && &selected.StencilMask(node.NativeOrdinal - 1) == this);
		}
		if (!genuine_owner) {
			throw std::runtime_error("motion stencil mask list requires its exact exclusive selected kind12 owner");
		}
		const auto& map = selected.m_Timeline.LayerIndexMap;
		const auto* names = m_MaskNames == nullptr ? nullptr : std::get_if<resource::PsbArray>(&m_MaskNames->Data);
		std::vector<const MotionSelectedLayerNode*> resolved;
		if ((m_Prepared.StencilType & 4) != 0 && names != nullptr) {
			const auto* entries = std::get_if<resource::PsbObject>(&map.Data);
			if ((entries == nullptr && !std::holds_alternative<std::monostate>(map.Data)) || (entries != nullptr && entries->size() > kMaximumStencilMaskNames)) {
				throw std::runtime_error("motion stencil layer index map requires its bounded native dictionary owner");
			}
			// Bounded dictionary construction avoids a names-by-entries scan; first duplicate keys retain PSB Find behavior.
			std::map<std::string_view, const resource::PsbValue*, std::less<>> lookup;
			if (entries != nullptr) {
				for (const auto& entry : *entries) {
					if (entry.first.size() > kMaximumMotionCommandNameBytes) {
						throw std::runtime_error("motion stencil layer index map key exceeds its bounded native name");
					}
					lookup.try_emplace(entry.first, &entry.second);
				}
			}
			resolved.reserve(names->size());
			for (const auto& value : *names) {
				const auto& text = std::get<std::string>(value.Data);
				const auto name = std::string_view(text).substr(0, text.find('\0'));
				const auto entry = lookup.find(name);
				if (entry == lookup.end()) {
					continue;
				}
				const auto* integer = std::get_if<std::int64_t>(&entry->second->Data);
				if (integer == nullptr || *integer < 0 || static_cast<std::uint64_t>(*integer) >= nodes.size()) {
					throw std::runtime_error("motion stencil mask map index exceeds actual selected nodes");
				}
				const auto& node = nodes[static_cast<std::size_t>(*integer)];
				if (node.Prepared->Type == 0 || node.Prepared->Type == 3) {
					resolved.push_back(&node);
				}
			}
		}
		for (const auto* node : resolved) {
			selected.m_Nodes[node->NativeOrdinal - 1].CompositeMask = true;
		}
		m_MaskLayers = std::move(resolved);
		m_Bound = true;
	}

	std::span<const MotionSelectedLayerNode* const> MotionStencilMaskOwner::MaskLayers() const {
		if (!m_Bound) {
			throw std::runtime_error("motion stencil mask list requires its actual selected binding phase");
		}

		return m_MaskLayers;
	}

	bool MotionStencilMaskOwner::MaskLayersBound() const noexcept {
		return m_Bound;
	}

} // namespace sgre::scene
