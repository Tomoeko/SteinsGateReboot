#include "sgre/script/affine_layer_bindings.hpp"

#include "sgre/scene/affine_atan2.hpp"
#include "sgre/scene/affine_layer_order_workspace.hpp"
#include "sgre/scene/affine_matrix.hpp"
#include "sgre/scene/affine_plane_transform.hpp"
#include "sgre/scene/affine_trigonometry.hpp"
#include "sgre/script/affine_layer_links.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/object_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

	struct AffineLayerBindingRecord {
		SquirrelObjectIdentity Instance;
		scene::LayerHandle Layer;
		scene::AffineLayerParameters Parameters;
		std::unique_ptr<ScriptAffineLayerMatrices> Matrices;
		std::unique_ptr<ScriptAffineLayerLinks> Links;
		ScriptAffineLayerTaskState Task;
		scene::LayerPoint PreviousScale{-1, -1};
		bool Processing = false;
		bool Drawing = false;
		bool Releasing = false;
	};

	class AffinePriorityCall {
	public:
		explicit AffinePriorityCall(bool& active)
		    : m_Active(active) {
			if (active) {
				throw std::runtime_error("AffineLayer priority manager cannot reenter its active update");
			}

			m_Active = true;
		}

		~AffinePriorityCall() {
			m_Active = false;
		}

	private:
		bool& m_Active;
	};

	static float RequireFiniteAffinePriorityDepth(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("AffineLayer priority comparison requires finite depth");
		}

		return value;
	}

	enum class AffineLayerAction : std::uint32_t {
		GetScalar = 1,
		SetScalar,
		GetFlip,
		SetFlip,
		GetPivot,
		SetPivot,
		SetZoom,
		GetOrder,
		SetOrder,
		GetDepth,
		GetLink,
		SetLink,
		AssignLinks,
		Assign,
		CalcMatrix,
		CalcImageMatrix,
		Invalidate,
		Contains,
		ContainsShape
	};

	struct AffineLayerMethod {
		AffineLayerAction Action;
		scene::AffineScalar Scalar = scene::AffineScalar::PositionX;
		scene::AffineAxis Axis = scene::AffineAxis::X;
		scene::AffinePivot Pivot = scene::AffinePivot::Zoom;
	};

	static float AffineNativeFloat(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*number);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		return 0;
	}

	static bool AffineNativeTruth(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return *number != 0;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	static std::array<std::uint8_t, 16> AffineMethodMetadata(const AffineLayerMethod& method) {
		const auto action = static_cast<std::uint32_t>(method.Action);
		std::array<std::uint8_t, 16> bytes{};
		for (std::size_t index = 0; index < 4; ++index) {
			bytes[index] = static_cast<std::uint8_t>(action >> (index * 8));
		}
		bytes[4] = static_cast<std::uint8_t>(method.Scalar);
		bytes[5] = static_cast<std::uint8_t>(method.Axis);
		bytes[6] = static_cast<std::uint8_t>(method.Pivot);

		return bytes;
	}

	static void RequireAffineMethodMetadata(SquirrelVm& vm, const AffineLayerMethod& method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("AffineLayer method requires its captured native userdata cell");
		}

		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = AffineMethodMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("AffineLayer native method metadata is invalid");
		}
	}

	class ScriptAffineLayerBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptAffineMatrixBindings& matrices, std::size_t maximum)
		    : Vm(vm), Objects(objects), Layers(layers), MatrixBindings(matrices), Maximum(maximum), PriorityWorkspace(maximum) {
			if (maximum == 0 || maximum > 65536) {
				throw std::runtime_error("AffineLayer requires a positive bounded native owner capacity");
			}
			Membership.reserve(maximum);
			PriorityOrder.resize(maximum);
			SortedMembership.resize(maximum);
			SubmittedPriorities.resize(maximum);
			PriorityComparison = [this](std::uint32_t left, std::uint32_t right) {
				return ComparePriority(PriorityMember(left), PriorityMember(right));
			};
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptAffineMatrixBindings& MatrixBindings;
		std::size_t Maximum;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::unique_ptr<AffineLayerBindingRecord>> Instances;
		std::vector<AffineLayerBindingRecord*> Membership;
		scene::AffineLayerOrderWorkspace PriorityWorkspace;
		scene::AffineLayerOrderWorkspace::Comparison PriorityComparison;
		std::vector<std::uint32_t> PriorityOrder;
		std::vector<AffineLayerBindingRecord*> SortedMembership;
		std::vector<float> SubmittedPriorities;
		scene::ParallelTaskSet* PriorityTaskOwner = nullptr;
		scene::ParallelTaskHandle PriorityTask;
		bool PriorityTaskReleased = false;
		bool UpdatingPriority = false;
		bool SortDirty = false;

		AffineLayerBindingRecord* Find(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				return nullptr;
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance)) {
				return nullptr;
			}

			return found->second.get();
		}

		AffineLayerBindingRecord& Record(const SquirrelValue& instance) const {
			auto* record = Find(instance);
			if (!record || record->Releasing) {
				throw std::runtime_error("bad instance");
			}

			return *record;
		}

		AffineLayerBindingRecord* Resolve(const SquirrelValue& value) const {
			const auto resolved = Vm.Type(value) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(value) : value;
			if (Vm.Type(resolved) != SquirrelValueType::Instance) {
				Vm.SetLastErrorInContext(Vm.RootContext(), std::string("the object is not a class instance"));

				return nullptr;
			}

			if (auto* record = Find(resolved)) {
				return record;
			}
			if (!Vm.IsInstanceOf(resolved, Class)) {
				Vm.SetLastErrorInContext(Vm.RootContext(), std::string("invalid type tag"));
			}

			return nullptr;
		}

		AffineLayerBindingRecord* Parent(AffineLayerBindingRecord& record) const {
			return Resolve(record.Links->ParentLink());
		}

		AffineLayerBindingRecord& PriorityMember(std::uint32_t index) const {
			if (index >= Membership.size() || Membership[index]->Releasing) {
				throw std::runtime_error("AffineLayer priority membership changed during its active sort");
			}

			return *Membership[index];
		}

		float PriorityPositionDepth(AffineLayerBindingRecord& record, std::array<AffineLayerBindingRecord*, 256>& path, std::size_t depth) const {
			if (depth == path.size() || std::find(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(depth), &record) != path.begin() + static_cast<std::ptrdiff_t>(depth)) {
				throw std::runtime_error("AffineLayer priority links contain a cycle or exceed their depth limit");
			}

			path[depth] = &record;
			auto* parent = Parent(record);
			const auto position = RequireFiniteAffinePriorityDepth(record.Parameters.State().PositionZ);
			if (!parent) {
				return position;
			}

			return RequireFiniteAffinePriorityDepth(PriorityPositionDepth(*parent, path, depth + 1) + position);
		}

		float PriorityDepth(AffineLayerBindingRecord& record) const {
			std::array<AffineLayerBindingRecord*, 256> path{};
			const auto position = PriorityPositionDepth(record, path, 0);
			const auto state = record.Parameters.State();

			return RequireFiniteAffinePriorityDepth((position + state.OffsetZ) + state.OriginZ);
		}

		bool ComparePriority(AffineLayerBindingRecord& left, AffineLayerBindingRecord& right) const {
			auto* left_owner = &left;
			auto* right_owner = &right;
			for (std::size_t step = 0; step != 512; ++step) {
				const auto left_depth = PriorityDepth(*left_owner);
				const auto right_depth = PriorityDepth(*right_owner);
				if (left_depth > right_depth) {
					return true;
				}
				if (left_depth != right_depth) {
					return false;
				}

				auto* left_parent = Parent(*left_owner);
				auto* right_parent = Parent(*right_owner);
				if (left_parent == right_owner) {
					return left_owner->Parameters.State().Order < 0;
				}
				if (right_parent == left_owner) {
					return right_owner->Parameters.State().Order >= 0;
				}
				if (left_parent == right_parent) {
					return left_owner->Parameters.State().Order < right_owner->Parameters.State().Order;
				}
				if (left_parent) {
					left_owner = left_parent;
				} else {
					right_owner = right_parent;
				}
			}

			throw std::runtime_error("AffineLayer priority ancestor comparison exceeds its traversal limit");
		}

		void UpdatePriorityOrder() {
			const AffinePriorityCall call(UpdatingPriority);
			if (Vm.IsClosed()) {
				throw std::runtime_error("AffineLayer priority manager requires its live VM and native owner domain");
			}
			if (!SortDirty) {
				return;
			}

			const auto count = Membership.size();
			auto order = std::span<std::uint32_t>(PriorityOrder).first(count);
			for (std::size_t index = 0; index != count; ++index) {
				if (Membership[index]->Releasing) {
					throw std::runtime_error("AffineLayer priority manager cannot sort a retiring native owner");
				}
				order[index] = static_cast<std::uint32_t>(index);
			}
			PriorityWorkspace.Sort(order, PriorityComparison);
			if (Vm.IsClosed()) {
				throw std::runtime_error("AffineLayer priority manager lost its VM during comparison");
			}

			for (std::size_t rank = 0; rank != count; ++rank) {
				auto& record = PriorityMember(order[rank]);
				SortedMembership[rank] = &record;
				SubmittedPriorities[rank] = scene::PrepareAffineLayerPriority(static_cast<std::uint32_t>(rank), record.Parameters.State().AbsoluteBase);
			}
			std::copy_n(SortedMembership.begin(), count, Membership.begin());
			for (std::size_t rank = 0; rank != count; ++rank) {
				Layers.SetPriority(Membership[rank]->Layer, SubmittedPriorities[rank]);
			}

			SortDirty = false;
		}

		void Invalidate(AffineLayerBindingRecord& record, scene::AffineParameterInvalidation change) {
			if (change.Sort) {
				SortDirty = true;
			}
			if (change.Matrix) {
				record.Links->Invalidate();
			}
		}

		AffineLayerBindingRecord* ArgumentOwner(const SquirrelValue& value) const {
			if (Vm.Type(value) != SquirrelValueType::Instance) {
				Vm.SetLastError(std::string("the object is not a class instance"));

				return nullptr;
			}
			if (!Vm.IsInstanceOf(value, Class)) {
				Vm.SetLastError(std::string("invalid type tag"));

				return nullptr;
			}

			return Find(value);
		}

		SquirrelValue LinkValue(AffineLayerBindingRecord& record) const {
			const auto& parent = record.Links->ParentLink();

			return Vm.Type(parent) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(parent) : parent;
		}

		float Depth(AffineLayerBindingRecord& record, std::size_t depth = 0) const {
			if (depth == 256) {
				throw std::runtime_error("AffineLayer linked depth exceeds its traversal limit");
			}

			auto* parent = Parent(record);
			const auto position = record.Parameters.State().PositionZ;

			return parent ? Depth(*parent, depth + 1) + position : position;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (UpdatingPriority) {
				throw std::runtime_error("AffineLayer membership cannot append during its active priority sort");
			}
			if (arguments.empty() || arguments.size() > 2 || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("invalid argument.");
			}
			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() >= Maximum) {
				throw std::runtime_error("AffineLayer native capacity is exhausted or the receiver is initialized");
			}

			auto record = std::make_unique<AffineLayerBindingRecord>();
			record->Instance = Vm.BorrowObjectIdentity(instance);
			auto* borrowed = record.get();
			Layers.ConstructMotionInstance(arguments);
			bool inserted = false;
			try {
				record->Layer = Layers.Handle(instance);
				record->Matrices = std::make_unique<ScriptAffineLayerMatrices>(Vm, MatrixBindings);
				ScriptAffineLayerLinkCallbacks callbacks;
				callbacks.NativeClass = Vm.BorrowObjectIdentity(Class);
				callbacks.FindOwner = [weak](const SquirrelValue& value) -> ScriptAffineLayerLinks* {
					const auto owner = weak.lock();
					auto* linked = owner ? owner->Find(value) : nullptr;

					return linked ? linked->Links.get() : nullptr;
				};
				callbacks.MarkMatrixDirty = [borrowed] {
					borrowed->Task.MatrixInvalidated = true;
				};
				callbacks.MarkSortDirty = [weak] {
					if (const auto owner = weak.lock()) {
						owner->SortDirty = true;
					}
				};
				record->Links = std::make_unique<ScriptAffineLayerLinks>(Vm, instance, std::move(callbacks));
				Instances.emplace(index, std::move(record));
				inserted = true;
				Membership.push_back(borrowed);
				SortDirty = true;
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				if (inserted) {
					Membership.erase(std::remove(Membership.begin(), Membership.end(), borrowed), Membership.end());
					Instances.erase(index);
				} else {
					record.reset();
				}
				try {
					Layers.ReleaseInstance(instance);
				} catch (...) {
				}

				std::rethrow_exception(failure);
			}
		}

		void Release(const SquirrelValue& instance) {
			if (UpdatingPriority) {
				throw std::runtime_error("AffineLayer membership cannot unlink during its active priority sort");
			}
			Layers.RequireCanReleaseInstance(instance);
			auto& record = Record(instance);
			if (record.Processing || record.Drawing) {
				throw std::runtime_error("AffineLayer cannot retire while its matrix/common task is active");
			}

			record.Releasing = true;
			std::exception_ptr failure;
			try {
				record.Links->Unlink();
				record.Links->DetachChildren();
			} catch (...) {
				failure = std::current_exception();
			}
			// Native manager removal occurs between detaching children and dropping link root fields.
			Membership.erase(std::remove(Membership.begin(), Membership.end(), &record), Membership.end());
			SortDirty = true;
			record.Links.reset();
			record.Matrices->Close();
			record.Matrices.reset();
			try {
				Layers.ReleaseInstance(instance);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		std::optional<SquirrelHostRoot> Callback(const SquirrelValue& instance, std::string_view name) {
			SquirrelValue value;
			try {
				value = Vm.ReadSlotInContext(Vm.RootContext(), instance, std::string(name));
			} catch (const SquirrelVmException&) {
				// Native sq_get failure pushes null and preserves the root VM diagnostic.
				return {};
			}

			auto method = Vm.Retain(value);
			const auto resolved = Vm.Type(value) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(value) : value;
			if (Vm.Type(resolved) != SquirrelValueType::Closure && Vm.Type(resolved) != SquirrelValueType::NativeClosure) {
				return {};
			}

			return method;
		}

		SquirrelValue Callable(const SquirrelHostRoot& method) const {
			const auto& value = method.Value();

			return Vm.Type(value) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(value) : value;
		}

		void CallWithoutResult(const SquirrelValue& instance, std::string_view name, std::span<const SquirrelValue> parameters) {
			const auto method = Callback(instance, name);
			if (!method) {
				return;
			}

			std::array<SquirrelValue, 3> arguments{};
			if (parameters.size() > arguments.size() - 1) {
				throw std::runtime_error("AffineLayer callback exceeds its recovered argument extent");
			}
			arguments[0] = instance;
			std::copy(parameters.begin(), parameters.end(), arguments.begin() + 1);
			try {
				static_cast<void>(Vm.InvokeWithoutResultInContext(Vm.RootContext(), Callable(*method), std::span<const SquirrelValue>(arguments).first(parameters.size() + 1)));
			} catch (const SquirrelVmException&) {
				// The original matrix/onScale task ignores script failure status and continues the common phase.
			}
		}

		bool Hit(const SquirrelValue& instance, std::string_view name, std::span<const SquirrelValue> parameters) {
			const auto& task = Record(instance).Task;
			if (!task.TransformPrepared) {
				throw std::runtime_error("AffineLayer hit testing requires an observed matrix task; native constructor leaves inverse fields uninitialized");
			}

			const auto method = Callback(instance, name);
			if (!method) {
				return false;
			}
			std::array<SquirrelValue, 4> arguments{};
			arguments[0] = instance;
			std::copy(parameters.begin(), parameters.end(), arguments.begin() + 1);
			try {
				const auto result = Vm.InvokeInContext(Vm.RootContext(), Callable(*method), std::span<const SquirrelValue>(arguments).first(parameters.size() + 1));
				const auto converted = TryNativeInteger(result.Value);
				if (!converted) {
					throw std::runtime_error("AffineLayer hit callback requires a numeric/boolean result; original failed conversion publishes an uninitialized local byte");
				}

				return static_cast<std::uint8_t>(static_cast<std::uint32_t>(*converted)) != 0;
			} catch (const SquirrelVmException&) {
				return false;
			}
		}

		void Calculate(AffineLayerBindingRecord& record, const SquirrelValue& matrix_wrapper, bool image) {
			auto& matrix = MatrixBindings.Matrix(matrix_wrapper);
			if (image) {
				scene::ApplyAffineImageMatrix(matrix, record.Parameters.State());

				return;
			}

			auto* parent = Parent(record);
			std::optional<scene::LayerPoint> linked;
			if (parent) {
				const auto position = parent->Parameters.State();
				linked = scene::LayerPoint{position.PositionX, position.PositionY};
			}
			scene::ApplyAffineLayerMatrix(matrix, record.Parameters.State(), linked);
		}

		void CallMatrix(const SquirrelValue& instance, std::string_view name, const SquirrelValue& matrix) {
			const auto outer = Vm.Retain(matrix);
			auto argument = Vm.Retain(outer.Value());
			auto method = Callback(instance, name);
			if (method) {
				{
					const auto copied = Vm.Retain(argument.Value());
					try {
						static_cast<void>(Vm.InvokeWithoutResultInContext(Vm.RootContext(), Callable(*method), std::array<SquirrelValue, 2>{instance, copied.Value()}));
					} catch (const SquirrelVmException&) {
					}
				}
				method.reset();
			}
			argument = {};
		}

		void Evaluate(AffineLayerBindingRecord& record, const SquirrelValue& instance) {
			if (!record.Task.MatrixInvalidated) {
				if (!record.Task.TransformPrepared) {
					throw std::runtime_error("AffineLayer matrix task requires an observed invalidation; native initial dirty byte is unresolved");
				}

				return;
			}

			auto& roots = *record.Matrices;
			for (const auto slot : {AffineLayerMatrixSlot::Composite, AffineLayerMatrixSlot::Local, AffineLayerMatrixSlot::Image}) {
				roots.Matrix(slot) = {};
			}
			auto& composite = roots.Matrix(AffineLayerMatrixSlot::Composite);
			const auto initial = record.Parameters.State();
			const auto scaled = initial.BaseScale != 1;
			if (scaled) {
				scene::ScaleAffineMatrix(composite, initial.BaseScale, initial.BaseScale);
			}
			scene::TranslateAffineMatrix(composite, initial.OriginX, initial.OriginY);
			CallMatrix(instance, "calcMatrix", roots.Wrapper(AffineLayerMatrixSlot::Local));
			const scene::AffineMatrix identity;
			if (roots.Matrix(AffineLayerMatrixSlot::Local).Rows != identity.Rows) {
				composite = scene::MultiplyAffineMatrices(composite, roots.Matrix(AffineLayerMatrixSlot::Local));
			}
			if (scaled) {
				// The callback may change the stored reciprocal; native reads it after calcMatrix returns.
				const auto reciprocal = record.Parameters.State().InverseBaseScale;
				scene::ScaleAffineMatrix(composite, reciprocal, reciprocal);
			}
			CallMatrix(instance, "calcImageMatrix", roots.Wrapper(AffineLayerMatrixSlot::Image));
			if (roots.Matrix(AffineLayerMatrixSlot::Image).Rows != identity.Rows) {
				composite = scene::MultiplyAffineMatrices(composite, roots.Matrix(AffineLayerMatrixSlot::Image));
			}

			record.Task.Projected = scene::PrepareAffinePlaneTransform(composite);
			record.Task.Inverse = scene::InvertAffinePlaneTransform(record.Task.Projected);
			record.Task.TransformPrepared = true;
			const auto plane = record.Task.Projected;
			float angle = 0;
			scene::LayerPoint scale{plane.XX, plane.YY};
			if (plane.YX != 0) {
				angle = scene::PrepareAffineAngle(plane.YX, plane.XX);
				const auto trig = scene::PrepareAffineTrigonometry(-angle);
				scale.X = plane.XX * trig.Cosine - plane.YX * trig.Sine;
				scale.Y = plane.YY * trig.Cosine + plane.XY * trig.Sine;
			}
			if (record.PreviousScale.X != scale.X || record.PreviousScale.Y != scale.Y) {
				CallWithoutResult(instance, "onScale", std::array<SquirrelValue, 2>{scale.X, scale.Y});
				record.PreviousScale = scale;
			}

			record.Task.Camera.Scale = {scale.X, scale.Y};
			record.Task.Camera.RotationRadians = angle;
			// Native onScale can reenter the stored transform through another callback before these reads.
			record.Task.Camera.Offset = {-record.Task.Projected.TranslationX, -record.Task.Projected.TranslationY};
			// Original058c1f/058c2b/058c45 update this same common camera before common Process.
			Layers.PublishAffineCamera(instance, record.Task.Camera);
			// Native never clears the matrix dirty byte here: later frames recompute while it remains set.
		}

		void Process(const SquirrelValue& instance, const std::function<void()>& common_process) {
			auto& record = Record(instance);
			if (!common_process || record.Processing || record.Drawing) {
				throw std::runtime_error("AffineLayer process requires its actual common phase and cannot reenter active native state");
			}

			const auto self = Vm.Retain(instance);
			record.Processing = true;
			try {
				Evaluate(record, instance);
				common_process();
			} catch (...) {
				record.Processing = false;

				throw;
			}
			record.Processing = false;
		}

		void Draw(const SquirrelValue& instance, std::uint32_t argument, const std::function<void(std::uint32_t)>& common_draw) {
			auto& record = Record(instance);
			if (!common_draw || record.Processing || record.Drawing) {
				throw std::runtime_error("AffineLayer draw requires its actual common phase and cannot reenter active native state");
			}

			record.Drawing = true;
			try {
				if (record.Task.MatrixInvalidated) {
					CallWithoutResult(instance, "onDraw", {});
					// Native Draw clears this byte after the callback, including callback invalidation or script failure.
					record.Task.MatrixInvalidated = false;
				}
				common_draw(argument);
			} catch (...) {
				record.Drawing = false;

				throw;
			}
			record.Drawing = false;
		}

		SquirrelNativeResult Invoke(const AffineLayerMethod& method, const SquirrelValue& receiver) {
			auto& record = Record(receiver);
			const auto value = Vm.NativeAllocatedStackValue(2);
			switch (method.Action) {
			case AffineLayerAction::GetScalar:
				return SquirrelNativeResult::ReturnValue(record.Parameters.Get(method.Scalar));
			case AffineLayerAction::SetScalar:
				Invalidate(record, record.Parameters.Set(method.Scalar, AffineNativeFloat(value)));
				break;
			case AffineLayerAction::GetFlip:
				return SquirrelNativeResult::ReturnValue(method.Axis == scene::AffineAxis::X ? record.Parameters.State().FlipX : record.Parameters.State().FlipY);
			case AffineLayerAction::SetFlip:
				Invalidate(record, record.Parameters.SetFlip(method.Axis, AffineNativeTruth(value)));
				break;
			case AffineLayerAction::GetPivot:
				return SquirrelNativeResult::ReturnValue(record.Parameters.GetPivot(method.Pivot, method.Axis));
			case AffineLayerAction::SetPivot:
				Invalidate(record, record.Parameters.SetPivot(method.Pivot, method.Axis, std::holds_alternative<std::monostate>(value) ? std::optional<float>{} : AffineNativeFloat(value)));
				break;
			case AffineLayerAction::SetZoom:
				Invalidate(record, record.Parameters.SetZoom(AffineNativeFloat(value)));
				break;
			case AffineLayerAction::GetOrder:
				return SquirrelNativeResult::ReturnValue(record.Parameters.State().Order);
			case AffineLayerAction::SetOrder:
				Invalidate(record, record.Parameters.SetOrder(TryNativeInteger(value).value_or(0)));
				break;
			case AffineLayerAction::GetDepth:
				return SquirrelNativeResult::ReturnValue(Depth(record));
			case AffineLayerAction::GetLink:
				return SquirrelNativeResult::ReturnValue(LinkValue(record));
			case AffineLayerAction::SetLink:
				record.Links->SetLink(value);
				break;
			case AffineLayerAction::AssignLinks:
				if (auto* source = ArgumentOwner(value)) {
					record.Links->AssignLinks(*source->Links);
				}
				break;
			case AffineLayerAction::Assign:
				if (auto* source = ArgumentOwner(value)) {
					Invalidate(record, record.Parameters.Assign(source->Parameters));
				} else {
					throw std::runtime_error("AffineLayer.assign requires a live native source; original null-source path is unsafe");
				}
				break;
			case AffineLayerAction::CalcMatrix:
				Calculate(record, value, false);
				break;
			case AffineLayerAction::CalcImageMatrix:
				Calculate(record, value, true);
				break;
			case AffineLayerAction::Invalidate:
				record.Links->Invalidate();
				break;
			case AffineLayerAction::Contains:
				return SquirrelNativeResult::ReturnValue(Contains(record, receiver, value, false));
			case AffineLayerAction::ContainsShape:
				return SquirrelNativeResult::ReturnValue(Contains(record, receiver, value, true));
			}

			return SquirrelNativeResult::NoValues();
		}

		bool Contains(AffineLayerBindingRecord& record, const SquirrelValue& instance, const SquirrelValue& value, bool shape) {
			const auto point_x = AffineNativeFloat(shape ? Vm.NativeAllocatedStackValue(3) : value);
			const auto point_y = AffineNativeFloat(Vm.NativeAllocatedStackValue(shape ? 4 : 3));
			const auto inverse = record.Task.Inverse;
			const auto x = (point_x * inverse.XX + point_y * inverse.XY) + inverse.TranslationX;
			const auto y = (point_x * inverse.YX + point_y * inverse.YY) + inverse.TranslationY;
			if (!shape) {
				return Hit(instance, "containsImage", std::array<SquirrelValue, 2>{x, y});
			}

			const auto* name = TryStringText(value);
			if (!name) {
				throw std::runtime_error("AffineLayer.containsShape generated-string lifetime requires the original conversion path");
			}

			return Hit(instance, "containsShapeImage", std::array<SquirrelValue, 3>{std::string(*name), x, y});
		}
	};

	ScriptAffineLayerBindings::ScriptAffineLayerBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptAffineMatrixBindings& matrices, std::size_t max_instances)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, matrices, max_instances)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(layers.LayerClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("AffineLayer.constructor", [weak](auto&, const auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("AffineLayer constructor services have expired");
			}
			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("AffineLayer.weakref", [weak](auto&, const auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("AffineLayer weakref requires its actual last native stack value");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto register_method = [&](std::string_view name, const AffineLayerMethod& method) {
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(AffineMethodMetadata(method))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(std::string("AffineLayer.") + std::string(name), [weak, method](auto& active, const auto arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("bad instance");
				}
				RequireAffineMethodMetadata(active, method);

				return owner->Invoke(method, arguments[0]);
			},
			                                               captures));
		};
		struct ScalarNames {
			const char* Name;
			scene::AffineScalar Scalar;
		};
		static constexpr std::array<ScalarNames, 25> scalars{{{"Xpos", scene::AffineScalar::PositionX}, {"Ypos", scene::AffineScalar::PositionY}, {"Zpos", scene::AffineScalar::PositionZ},
		    {"Rotate", scene::AffineScalar::Rotation}, {"Zoomx", scene::AffineScalar::ZoomX}, {"Zoomy", scene::AffineScalar::ZoomY},
		    {"Slantx", scene::AffineScalar::SlantX}, {"Slanty", scene::AffineScalar::SlantY}, {"Offsetx", scene::AffineScalar::OffsetX},
		    {"Offsety", scene::AffineScalar::OffsetY}, {"Offsetz", scene::AffineScalar::OffsetZ}, {"Centerx", scene::AffineScalar::CenterX},
		    {"Centery", scene::AffineScalar::CenterY}, {"Originx", scene::AffineScalar::OriginX}, {"Originy", scene::AffineScalar::OriginY},
		    {"Originz", scene::AffineScalar::OriginZ}, {"AbsoluteBase", scene::AffineScalar::AbsoluteBase}, {"BaseScale", scene::AffineScalar::BaseScale},
		    {"Imagex", scene::AffineScalar::ImageX}, {"Imagey", scene::AffineScalar::ImageY}, {"Imagerotate", scene::AffineScalar::ImageRotation},
		    {"Imagezoom", scene::AffineScalar::ImageZoom}, {"Left", scene::AffineScalar::PositionX}, {"Top", scene::AffineScalar::PositionY},
		    {"Depth", scene::AffineScalar::PositionZ}}};
		for (const auto& scalar : scalars) {
			AffineLayerMethod method{AffineLayerAction::GetScalar};
			method.Scalar = scalar.Scalar;
			register_method(std::string("get") + scalar.Name, method);
			method.Action = AffineLayerAction::SetScalar;
			register_method(std::string("set") + scalar.Name, method);
		}
		for (const auto axis : {scene::AffineAxis::X, scene::AffineAxis::Y}) {
			const auto suffix = axis == scene::AffineAxis::X ? "x" : "y";
			AffineLayerMethod method{AffineLayerAction::GetFlip};
			method.Axis = axis;
			register_method(std::string("getFlip") + suffix, method);
			method.Action = AffineLayerAction::SetFlip;
			register_method(std::string("setFlip") + suffix, method);
			for (const auto pivot : {scene::AffinePivot::Rotation, scene::AffinePivot::Zoom}) {
				method.Action = AffineLayerAction::GetPivot;
				method.Pivot = pivot;
				const auto prefix = pivot == scene::AffinePivot::Rotation ? "Rcenter" : "Zcenter";
				register_method(std::string("get") + prefix + suffix, method);
				method.Action = AffineLayerAction::SetPivot;
				register_method(std::string("set") + prefix + suffix, method);
			}
		}
		register_method("setZoom", {AffineLayerAction::SetZoom});
		register_method("getOrder", {AffineLayerAction::GetOrder});
		register_method("setOrder", {AffineLayerAction::SetOrder});
		register_method("getCalczpos", {AffineLayerAction::GetDepth});
		register_method("getLink", {AffineLayerAction::GetLink});
		register_method("setLink", {AffineLayerAction::SetLink});
		register_method("assignLink", {AffineLayerAction::AssignLinks});
		register_method("assign", {AffineLayerAction::Assign});
		register_method("calcMatrix", {AffineLayerAction::CalcMatrix});
		register_method("calcImageMatrix", {AffineLayerAction::CalcImageMatrix});
		register_method("updateAffine", {AffineLayerAction::Invalidate});
		register_method("contains", {AffineLayerAction::Contains});
		register_method("containsShape", {AffineLayerAction::ContainsShape});
		vm.NewSlot(vm.RootTable(), std::string("AffineLayer"), state.Class);
	}

	ScriptAffineLayerBindings::~ScriptAffineLayerBindings() noexcept {
		try {
			DetachPriorityTask();
		} catch (...) {
			std::terminate();
		}
	}

	SquirrelValue ScriptAffineLayerBindings::AffineLayerClass() const {
		return m_Implementation->Class;
	}

	scene::AffineLayerParameterState ScriptAffineLayerBindings::Parameters(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Parameters.State();
	}

	scene::AffineMatrix ScriptAffineLayerBindings::MatrixValue(const SquirrelValue& instance, AffineLayerMatrixSlot slot) const {
		return m_Implementation->Record(instance).Matrices->Matrix(slot);
	}

	ScriptAffineLayerTaskState ScriptAffineLayerBindings::TaskState(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Task;
	}

	bool ScriptAffineLayerBindings::PriorityOrderDirty() const noexcept {
		return m_Implementation->SortDirty;
	}

	void ScriptAffineLayerBindings::UpdatePriorityOrder() {
		m_Implementation->UpdatePriorityOrder();
	}

	void ScriptAffineLayerBindings::AttachPriorityTask(scene::ParallelTaskSet& actual_driver_group) {
		auto& state = *m_Implementation;
		if (state.PriorityTaskOwner || state.UpdatingPriority || !actual_driver_group.CanClear() || actual_driver_group.Status() != scene::TaskStatus::Active || state.Vm.IsClosed()) {
			throw std::runtime_error("AffineLayer priority task requires one stable actual default SQDriver parallel owner");
		}

		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [weak](auto) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("AffineLayer priority task lost its native manager adapter");
			}

			owner->UpdatePriorityOrder();
		};
		// Native manager Draw is exactly ret0; this task contains no renderer or source drawing substitute.
		callbacks.Release = [weak](auto) {
			if (const auto owner = weak.lock()) {
				owner->PriorityTaskReleased = true;
			}
		};
		state.PriorityTask = actual_driver_group.Append(std::move(callbacks));
		state.PriorityTaskOwner = &actual_driver_group;
		state.PriorityTaskReleased = false;
	}

	bool ScriptAffineLayerBindings::CanDetachPriorityTask() const noexcept {
		const auto& state = *m_Implementation;
		if (state.UpdatingPriority) {
			return false;
		}
		if (!state.PriorityTaskOwner || state.PriorityTaskReleased) {
			return true;
		}

		return state.PriorityTaskOwner->CanExtract(state.PriorityTask);
	}

	void ScriptAffineLayerBindings::DetachPriorityTask() {
		auto& state = *m_Implementation;
		if (!CanDetachPriorityTask()) {
			throw std::runtime_error("AffineLayer priority task cannot detach during traversal or while a transferred adapter remains alive");
		}
		if (state.PriorityTaskOwner && !state.PriorityTaskReleased) {
			auto task = state.PriorityTaskOwner->Extract(state.PriorityTask);
			task.Release();
		}

		state.PriorityTaskOwner = nullptr;
		state.PriorityTask = {};
		state.PriorityTaskReleased = false;
	}

	void ScriptAffineLayerBindings::Process(const SquirrelValue& instance, const std::function<void()>& common_process) {
		m_Implementation->Process(instance, common_process);
	}

	void ScriptAffineLayerBindings::Draw(const SquirrelValue& instance, std::uint32_t argument, const std::function<void(std::uint32_t)>& common_draw) {
		m_Implementation->Draw(instance, argument, common_draw);
	}

	void ScriptAffineLayerBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
