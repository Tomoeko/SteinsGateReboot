#pragma once

#include "sgre/script/affine_matrix_bindings.hpp"

#include <array>
#include <cstdint>

namespace sgre::script {

	enum class AffineLayerMatrixSlot : std::uint8_t {
		Composite,
		Local,
		Image
	};

	class ScriptAffineLayerMatrices {
	public:
		// VM and genuine matrix bindings remain live through all three root retirements.
		ScriptAffineLayerMatrices(SquirrelVm& vm, ScriptAffineMatrixBindings& matrices);
		~ScriptAffineLayerMatrices();
		ScriptAffineLayerMatrices(const ScriptAffineLayerMatrices&) = delete;
		ScriptAffineLayerMatrices& operator=(const ScriptAffineLayerMatrices&) = delete;
		ScriptAffineLayerMatrices(ScriptAffineLayerMatrices&&) = delete;
		ScriptAffineLayerMatrices& operator=(ScriptAffineLayerMatrices&&) = delete;

		[[nodiscard]] const SquirrelValue& Wrapper(AffineLayerMatrixSlot slot) const;
		// Borrows a separately owned native matrix; script copies may survive this root owner.
		[[nodiscard]] scene::AffineMatrix& Matrix(AffineLayerMatrixSlot slot);
		[[nodiscard]] bool IsClosed() const noexcept;
		void Close() noexcept;

	private:
		ScriptAffineMatrixBindings& m_Matrices;
		std::array<SquirrelHostRoot, 3> m_Wrappers;
		bool m_Closed = false;
	};

} // namespace sgre::script
