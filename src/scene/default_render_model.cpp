#include "sgre/scene/default_render_model.hpp"

namespace sgre::scene {
	MotionScissorMatrix PrepareDefaultRenderModel() {
		MotionScissorMatrix model;
		// Original4c53d0/530b90 looks from (0,0,-1) with downward Y and preserves signed zero translations.
		model.Coefficients[3] = -0.0f;
		model.Coefficients[5] = -1.0f;
		model.Coefficients[7] = -0.0f;
		model.Coefficients[10] = -1.0f;
		model.Coefficients[11] = -1.0f;

		return model;
	}
} // namespace sgre::scene
