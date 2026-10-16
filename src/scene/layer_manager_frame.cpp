#include "sgre/scene/layer_manager_frame.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::scene {

	class LayerManagerFrameCall {
	public:
		explicit LayerManagerFrameCall(bool& calling)
		    : m_Calling(calling) {
			if (calling) {
				throw std::runtime_error("actual Layer manager frame cannot reenter update or drawing");
			}

			m_Calling = true;
		}

		~LayerManagerFrameCall() {
			m_Calling = false;
		}

	private:
		bool& m_Calling;
	};

	LayerManagerFrame::LayerManagerFrame(LayerTree& tree, LayerFrameManager manager, LayerManagerFrameOperations operations, LayerManagerDrawMode draw_mode)
	    : m_Tree(tree), m_Manager(std::move(manager)), m_Operations(std::move(operations)), m_DrawMode(draw_mode) {
		if (draw_mode != LayerManagerDrawMode::ResetRenderer && draw_mode != LayerManagerDrawMode::PreserveRenderer) {
			throw std::runtime_error("actual Layer manager draw mode is outside its recovered reset/preserve branch");
		}
		if (!m_Operations.State || !m_Operations.Update || !m_Operations.Draw || (draw_mode == LayerManagerDrawMode::ResetRenderer && !m_Operations.PrepareDraw)) {
			throw std::runtime_error("actual Layer manager frame requires concrete state, update, draw and renderer preparation services");
		}

		m_UpdateVisit = [this](LayerHandle layer) {
			const auto state = State(layer);
			if (state.Status != TaskStatus::Active) {
				return;
			}
			if (state.ThreadedUpdate) {
				throw std::runtime_error("actual Layer threaded update requires its separately verified worker dispatcher");
			}

			m_Operations.Update(layer);
		};
		m_DrawVisit = [this](LayerHandle layer) {
			if (m_Tree.State(layer).Visible && State(layer).Status != TaskStatus::Retire) {
				m_Operations.Draw(layer, m_DrawArgument);
			}
		};
	}

	LayerFrameProcessState LayerManagerFrame::State(LayerHandle layer) const {
		const auto state = m_Operations.State(layer);
		if (state.Status != TaskStatus::Active && state.Status != TaskStatus::Complete && state.Status != TaskStatus::Retire) {
			throw std::runtime_error("actual Layer process status is outside the recovered 0..2 domain");
		}

		return state;
	}

	void LayerManagerFrame::Visit(const std::function<void(LayerHandle)>& operation) {
		if (const auto* screen = std::get_if<LayerScreenRootHandle>(&m_Manager)) {
			m_Tree.VisitScreenChildren(*screen, operation);
		} else if (const auto* folder = std::get_if<LayerHandle>(&m_Manager)) {
			m_Tree.VisitChildren(*folder, operation);
		} else {
			m_Tree.VisitChildren({}, operation);
		}
	}

	void LayerManagerFrame::PrepareOrder() {
		if (const auto* screen = std::get_if<LayerScreenRootHandle>(&m_Manager)) {
			m_Tree.PrepareScreenChildren(*screen);
		} else if (const auto* folder = std::get_if<LayerHandle>(&m_Manager)) {
			m_Tree.PrepareChildren(*folder);
		} else {
			m_Tree.PrepareChildren();
		}
	}

	void LayerManagerFrame::Update() {
		const LayerManagerFrameCall call(m_Calling);
		Visit(m_UpdateVisit);
	}

	void LayerManagerFrame::Draw(std::uint32_t argument) {
		const LayerManagerFrameCall call(m_Calling);
		m_DrawArgument = argument;
		PrepareOrder();
		if (m_DrawMode == LayerManagerDrawMode::ResetRenderer) {
			m_Operations.PrepareDraw();
		}
		Visit(m_DrawVisit);
	}

} // namespace sgre::scene
