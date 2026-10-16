#pragma once

#include "sgre/scene/affine_matrix.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <memory>

namespace sgre::script {

	class ScriptAffineMatrixBindings {
	public:
		// The VM outlives this service and every script matrix release callback.
		explicit ScriptAffineMatrixBindings(SquirrelVm& vm, std::size_t maximum_instances = 4096);
		~ScriptAffineMatrixBindings();
		ScriptAffineMatrixBindings(const ScriptAffineMatrixBindings&) = delete;
		ScriptAffineMatrixBindings& operator=(const ScriptAffineMatrixBindings&) = delete;
		ScriptAffineMatrixBindings(ScriptAffineMatrixBindings&&) = delete;
		ScriptAffineMatrixBindings& operator=(ScriptAffineMatrixBindings&&) = delete;

		[[nodiscard]] SquirrelValue MatrixClass() const;
		// Native wrap-owned matrix operation; this creates an instance without invoking constructor.
		[[nodiscard]] SquirrelValue WrapMatrix(scene::AffineMatrix matrix = {});
		// Borrows the owned matrix until its script instance retires or the service is destroyed.
		[[nodiscard]] scene::AffineMatrix& Matrix(const SquirrelValue& instance);
		[[nodiscard]] std::size_t InstanceCount() const;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
