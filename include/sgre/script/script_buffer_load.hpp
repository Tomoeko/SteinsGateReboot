#pragma once

#include "sgre/io/script_buffer_reader.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace sgre::script {
	class ScriptBufferLoad {
	public:
		// Owns the actual filename field (native+58) and singular buffer (native+70), not a Resource target.
		// VM/cache owners outlive this load. Buffer bytes have no script-instance root.
		ScriptBufferLoad(SquirrelVm& vm, const SquirrelValue& actual_filename, resource::ResourceCache& actual_cache);
		~ScriptBufferLoad() noexcept;
		ScriptBufferLoad(const ScriptBufferLoad&) = delete;
		ScriptBufferLoad& operator=(const ScriptBufferLoad&) = delete;
		ScriptBufferLoad(ScriptBufferLoad&&) = delete;
		ScriptBufferLoad& operator=(ScriptBufferLoad&&) = delete;
		[[nodiscard]] const SquirrelValue& FilenameValue() const;
		[[nodiscard]] std::string ReadFilename() const;
		[[nodiscard]] bool HasBuffer() const noexcept;
		[[nodiscard]] io::ScriptReadBuffer& Buffer();
		// A manager hit appends no reader; a miss appends a genuine fresh reader borrowing this load-owned cell.
		[[nodiscard]] std::optional<scene::ParallelTaskHandle> AppendMountedRead(scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, std::string_view seed);
		// Real compiled-only parser/VM closure production; buffer is released after success or failure, before Running.
		// The original filename remains owned until explicit load/thread retirement. Native source/error parity stays open.
		[[nodiscard]] SquirrelValue LoadCompiledClosureAndReleaseBuffer();
		[[nodiscard]] SquirrelValue LoadCompiledClosureAndReleaseBufferInContext(SquirrelContextHandle actual_child);
		// Explicit ready-cell retirement also covers a child-context allocation failure before loader entry.
		void ReleaseReadyBuffer();
		[[nodiscard]] bool CanClose() const noexcept;
		void RequireCanClose() const;
		void Close();

	private:
		[[nodiscard]] SquirrelValue LoadCompiledInOptionalContext(std::optional<SquirrelContextHandle> actual_child);
		void RequireOwnerThread() const;
		SquirrelVm& m_Vm;
		SquirrelHostRoot m_Filename;
		std::unique_ptr<io::ScriptReadBuffer> m_Buffer;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		bool m_Closed = false;
	};
} // namespace sgre::script
