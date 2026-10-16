#pragma once

#include "sgre/core/background_calls.hpp"
#include "sgre/save/backup_files.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/struct_bindings.hpp"

#include <functional>
#include <memory>

namespace sgre::script {

	class ScriptBackupBindings {
	public:
		// Actual VM, Layer/Struct adapters, background owner and directory callback enclose VM Close.
		// The application supplies a real ignored rewrite save directory, never the reference installation.
		ScriptBackupBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptStructBindings& structs, core::BackgroundCallManager& background, std::function<bool()> initialize_directory);
		~ScriptBackupBindings();
		ScriptBackupBindings(const ScriptBackupBindings&) = delete;
		ScriptBackupBindings& operator=(const ScriptBackupBindings&) = delete;
		ScriptBackupBindings(ScriptBackupBindings&&) = delete;
		ScriptBackupBindings& operator=(ScriptBackupBindings&&) = delete;
		// The exact application directory/codec configuration is sealed before the first native manager.
		void ConfigureFiles(save::BackupFileServices services);
		[[nodiscard]] SquirrelValue ManagerClass() const;
		[[nodiscard]] SquirrelValue SegmentClass() const;
		[[nodiscard]] SquirrelValue AdventureManagerClass() const;
		[[nodiscard]] SquirrelValue AdventureSegmentClass() const;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
