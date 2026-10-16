#include "sgre/script/script_buffer_load.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::script {
	ScriptBufferLoad::ScriptBufferLoad(SquirrelVm& vm, const SquirrelValue& actual_filename, resource::ResourceCache& actual_cache) : m_Vm(vm) {
		const auto& name = StringText(actual_filename);
		if (vm.IsClosed() || name.empty() || name.size() > 4096 || name.find('\0') != std::string::npos) {
			throw std::runtime_error("script buffer load requires its actual live VM and bounded original filename");
		}

		m_Filename = vm.Retain(actual_filename);
		m_Buffer = std::make_unique<io::ScriptReadBuffer>(actual_cache, name);
	}

	ScriptBufferLoad::~ScriptBufferLoad() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void ScriptBufferLoad::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed) {
			throw std::runtime_error("script buffer load requires its actual live receiving-thread owner");
		}
	}

	const SquirrelValue& ScriptBufferLoad::FilenameValue() const {
		RequireOwnerThread();

		return m_Filename.Value();
	}

	std::string ScriptBufferLoad::ReadFilename() const {
		RequireOwnerThread();

		return StringText(m_Filename.Value()) + ".m";
	}

	bool ScriptBufferLoad::HasBuffer() const noexcept {
		return m_Buffer != nullptr;
	}

	io::ScriptReadBuffer& ScriptBufferLoad::Buffer() {
		RequireOwnerThread();
		if (!m_Buffer) {
			throw std::runtime_error("script load buffer has already retired after its real loader handoff");
		}

		return *m_Buffer;
	}

	std::optional<scene::ParallelTaskHandle> ScriptBufferLoad::AppendMountedRead(scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, std::string_view seed) {
		RequireOwnerThread();
		if (Buffer().Storage() == io::ScriptBufferStorage::ManagerBinaryPin) {
			return std::nullopt;
		}

		auto source = io::PrepareQueuedMountedScriptBufferRead(mounts, ReadFilename(), seed);

		return io::AppendQueuedMountedScriptBufferReader(actual_driver, background, mounts, Buffer(), std::move(source));
	}

	SquirrelValue ScriptBufferLoad::LoadCompiledClosureAndReleaseBuffer() {
		return LoadCompiledInOptionalContext(std::nullopt);
	}

	SquirrelValue ScriptBufferLoad::LoadCompiledClosureAndReleaseBufferInContext(SquirrelContextHandle actual_child) {
		return LoadCompiledInOptionalContext(actual_child);
	}

	SquirrelValue ScriptBufferLoad::LoadCompiledInOptionalContext(std::optional<SquirrelContextHandle> actual_child) {
		RequireOwnerThread();
		const auto observed = Buffer().Observe();
		if (!observed.Ready) {
			throw std::runtime_error("script compilation requires its separate actual reader readiness gate");
		}

		try {
			if (m_Vm.IsClosed()) {
				throw std::runtime_error("script compilation requires the actual live child/VM arena owner");
			}
			if (observed.Completion != io::FileReadCompletion::Succeeded) {
				throw std::runtime_error("script buffer has no successful payload: " + m_Buffer->Error());
			}
			if (observed.Bytes.size() < 2 || observed.Bytes[0] != 0xfa || observed.Bytes[1] != 0xfa) {
				throw std::runtime_error("script buffer source-text/BOM compilation is unrecovered: " + StringText(m_Filename.Value()));
			}

			auto program = ReadSquirrelBytecode(observed.Bytes);
			auto closure = actual_child ? m_Vm.CreateClosureInContext(*actual_child, std::move(program)) : m_Vm.CreateClosure(std::move(program));
			m_Buffer.reset();

			return closure;
		} catch (...) {
			m_Buffer.reset();
			throw;
		}
	}

	void ScriptBufferLoad::ReleaseReadyBuffer() {
		RequireOwnerThread();
		if (!Buffer().Observe().Ready) {
			throw std::runtime_error("buffer retirement requires actual reader readiness or a prior explicit reader fence");
		}
		m_Buffer.reset();
	}

	bool ScriptBufferLoad::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (!m_Buffer || m_Buffer->CanClose());
	}

	void ScriptBufferLoad::RequireCanClose() const {
		if (!CanClose()) {
			throw std::runtime_error("script load retirement must preflight its buffer before filename/derived roots drop");
		}
	}

	void ScriptBufferLoad::Close() {
		RequireCanClose();
		if (m_Closed) {
			return;
		}

		m_Buffer.reset();
		m_Filename = {};
		m_Closed = true;
	}
} // namespace sgre::script
