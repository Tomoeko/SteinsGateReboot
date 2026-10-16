#include "sgre/script/affine_layer_matrices.hpp"

#include <cstddef>
#include <stdexcept>

namespace sgre::script {

	static std::size_t AffineMatrixSlotIndex(AffineLayerMatrixSlot slot) {
		switch (slot) {
		case AffineLayerMatrixSlot::Composite:
			return 0;
		case AffineLayerMatrixSlot::Local:
			return 1;
		case AffineLayerMatrixSlot::Image:
			return 2;
		}

		throw std::runtime_error("invalid AffineLayer matrix slot");
	}

	ScriptAffineLayerMatrices::ScriptAffineLayerMatrices(SquirrelVm& vm, ScriptAffineMatrixBindings& matrices) : m_Matrices(matrices) {
		for (auto& root : m_Wrappers) {
			root = vm.Retain(matrices.WrapMatrix());
		}
	}

	ScriptAffineLayerMatrices::~ScriptAffineLayerMatrices() {
		Close();
	}

	const SquirrelValue& ScriptAffineLayerMatrices::Wrapper(AffineLayerMatrixSlot slot) const {
		if (m_Closed) {
			throw std::runtime_error("AffineLayer matrix roots have retired");
		}

		return m_Wrappers[AffineMatrixSlotIndex(slot)].Value();
	}

	scene::AffineMatrix& ScriptAffineLayerMatrices::Matrix(AffineLayerMatrixSlot slot) {
		return m_Matrices.Matrix(Wrapper(slot));
	}

	bool ScriptAffineLayerMatrices::IsClosed() const noexcept {
		return m_Closed;
	}

	void ScriptAffineLayerMatrices::Close() noexcept {
		if (m_Closed) {
			return;
		}

		m_Closed = true;
		// Actual SQAffineLayer retires image, local, then composite before the common SQLayer owner.
		for (std::size_t index = m_Wrappers.size(); index > 0; --index) {
			m_Wrappers[index - 1] = {};
		}
	}

} // namespace sgre::script
