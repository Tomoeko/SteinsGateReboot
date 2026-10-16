#include "sgre/script/backup_bindings.hpp"

#include "sgre/save/backup_manager.hpp"

#include <bit>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::script {

	static void RequireBackupArguments(std::span<const SquirrelValue> arguments, std::size_t count) {
		if (arguments.size() != count) {
			throw std::runtime_error("invalid argument.");
		}
	}

	static std::string_view BackupString(const SquirrelValue& value) {
		const auto* text = TryStringText(value);
		if (text == nullptr) {
			throw std::runtime_error("Backup metadata requires its actual native string");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	static std::int32_t BackupInteger(const SquirrelValue& value) {
		return TryNativeInteger(value).value_or(0);
	}

	static bool BackupBoolean(const SquirrelValue& value) {
		// The native bool adapter uses sq_tobool, independently of sq_getinteger.
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer != 0;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0.0f;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	class ScriptBackupBindings::Implementation {
	public:
		struct ManagerRecord {
			SquirrelObjectIdentity Instance;
			// Actual mutable Struct receivers enclose Owner even during constructor unwind or binding destruction.
			std::vector<SquirrelHostRoot> StructOwners;
			std::unique_ptr<save::BackupManager> Owner;
		};

		struct SegmentRecord {
			SquirrelObjectIdentity Instance;
			SquirrelObjectIdentity Manager;
			std::uint32_t Ordinal = 0;
			std::string DialogName;
		};

		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptStructBindings& structs, core::BackgroundCallManager& background, std::function<bool()> initialize_directory)
		    : Vm(vm), Objects(objects), Layers(layers), Structs(structs), Background(background), InitializeDirectory(std::move(initialize_directory)) {
			if (!InitializeDirectory) {
				throw std::runtime_error("Backup bindings require their actual platform directory initialization");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptStructBindings& Structs;
		core::BackgroundCallManager& Background;
		std::function<bool()> InitializeDirectory;
		SquirrelValue ManagerClass;
		SquirrelValue SegmentClass;
		SquirrelValue AdventureManagerClass;
		SquirrelValue AdventureSegmentClass;
		SquirrelHostRoot ManagerClassRoot;
		SquirrelHostRoot SegmentClassRoot;
		SquirrelHostRoot AdventureManagerClassRoot;
		SquirrelHostRoot AdventureSegmentClassRoot;
		std::map<std::uint32_t, ManagerRecord> Managers;
		std::map<std::uint32_t, SegmentRecord> Segments;
		std::weak_ptr<Implementation> Owner;
		std::optional<save::BackupFileServices> Files;
		bool ManagerConstructionStarted = false;
		std::uint8_t ManagerTag = 0;
		std::uint8_t SegmentTag = 0;
		std::uint8_t AdventureManagerTag = 0;
		std::uint8_t AdventureSegmentTag = 0;

		ManagerRecord& Manager(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("BackupManager requires its real derived Layer receiver");
			}
			const auto found = Managers.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Managers.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Objects.HasInstance(instance) || found->second.Owner->IsClosed()) {
				throw std::runtime_error("BackupManager receiver has no live actual native owner");
			}

			return found->second;
		}

		SegmentRecord& Segment(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("BackupSegment requires its real native receiver");
			}
			const auto found = Segments.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Segments.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Objects.HasInstance(instance)) {
				throw std::runtime_error("BackupSegment receiver has no actual manager-owned segment");
			}
			const auto manager = Managers.find(found->second.Manager.Index);
			if (manager == Managers.end() || manager->second.Instance != found->second.Manager || manager->second.Owner->IsClosed()) {
				throw std::runtime_error("BackupSegment outlived its actual native manager");
			}

			return found->second;
		}

		save::BackupManager& SegmentManager(const SegmentRecord& segment) {
			return *Managers.at(segment.Manager.Index).Owner;
		}

		save::BackupSegment& SegmentOwner(const SquirrelValue& instance) {
			const auto& segment = Segment(instance);

			return SegmentManager(segment).Segment(segment.Ordinal);
		}

		void RequireManagerRetirement(const SquirrelValue& receiver) {
			const auto& record = Manager(receiver);
			if (!record.Owner->CanClose()) {
				throw std::runtime_error("BackupManager retirement retains an actual camera/background task borrow");
			}
		}

		void ReleaseManager(const SquirrelValue& receiver) {
			const auto index = std::get<SquirrelObjectHandle>(receiver).Index;
			const auto found = Managers.find(index);
			if (found == Managers.end() || found->second.Instance != Vm.BorrowObjectIdentity(receiver)) {
				throw std::runtime_error("BackupManager cleanup lost its actual native identity");
			}

			std::exception_ptr failure;
			try {
				found->second.Owner->Close();
			} catch (...) {
				if (!found->second.Owner->IsClosed()) {
					throw;
				}
				failure = std::current_exception();
			}
			Managers.erase(found);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void ConstructManager(std::span<const SquirrelValue> arguments, save::BackupProfile profile) {
			if (arguments.empty() || Managers.size() >= 4096 || !Vm.IsInstanceOf(arguments[0], profile == save::BackupProfile::Adventure ? AdventureManagerClass : ManagerClass)) {
				throw std::runtime_error("BackupManager constructor has no receiver or exceeds its bounded native records");
			}

			Layers.ConstructBaseInstance(arguments);
			ManagerConstructionStarted = true;
			try {
				ManagerRecord record;
				record.Instance = Vm.BorrowObjectIdentity(arguments[0]);
				record.Owner = std::make_unique<save::BackupManager>(Background, Layers.BaseTasks(arguments[0]).Children(), InitializeDirectory, 1024, 128, profile);
				if (profile == save::BackupProfile::Adventure && Files) {
					record.Owner->ConfigureFiles(*Files);
				}
				const auto index = record.Instance.Index;
				if (!Managers.emplace(index, std::move(record)).second) {
					throw std::runtime_error("BackupManager receiver already owns a native manager");
				}
				const auto weak = Owner;
				Layers.ConfigureBaseRetirement(
				    arguments[0], [weak](const auto& receiver) {
					    const auto owner = weak.lock();
					    if (!owner) {
						    throw std::runtime_error("BackupManager binding owner expired before its Layer");
					    }
					    owner->RequireManagerRetirement(receiver);
				    },
				    [weak](const auto& receiver) {
					    const auto owner = weak.lock();
					    if (!owner) {
						    throw std::runtime_error("BackupManager binding owner expired before native cleanup");
					    }
					    owner->ReleaseManager(receiver);
				    });
			} catch (...) {
				const auto failure = std::current_exception();
				if (Managers.contains(std::get<SquirrelObjectHandle>(arguments[0]).Index)) {
					ReleaseManager(arguments[0]);
				}
				Layers.ReleaseInstance(arguments[0]);
				std::rethrow_exception(failure);
			}
		}

		SquirrelValue WrapSegment(ManagerRecord& manager, std::uint32_t ordinal) {
			static_cast<void>(manager.Owner->Segment(ordinal));
			if (Segments.size() >= 65536) {
				throw std::runtime_error("BackupSegment wrapper capacity exceeded");
			}

			const auto instance = Vm.CreateInstance(manager.Owner->Profile() == save::BackupProfile::Adventure ? AdventureSegmentClass : SegmentClass);
			SegmentRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Manager = manager.Instance;
			record.Ordinal = ordinal;
			Objects.AttachInstance(instance);
			try {
				if (!Segments.emplace(record.Instance.Index, std::move(record)).second) {
					throw std::runtime_error("BackupSegment receiver already owns its native wrapper");
				}
				const auto weak = Owner;
				Objects.ConfigureNativeRelease(instance, [weak](const auto& receiver) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("BackupSegment binding expired before native cleanup");
					}
					const auto index = std::get<SquirrelObjectHandle>(receiver).Index;
					const auto found = owner->Segments.find(index);
					if (found == owner->Segments.end() || found->second.Instance != owner->Vm.BorrowObjectIdentity(receiver)) {
						throw std::runtime_error("BackupSegment cleanup identity is stale");
					}
					owner->Segments.erase(found);
					owner->Objects.ReleaseInstance(receiver);
				});
			} catch (...) {
				Segments.erase(std::get<SquirrelObjectHandle>(instance).Index);
				Objects.ReleaseInstance(instance);
				throw;
			}

			return instance;
		}

		void LoadAdventure(std::span<const SquirrelValue> arguments) {
			if (arguments.empty() || arguments.size() > 2) {
				throw std::runtime_error("invalid argument.");
			}
			auto& manager = Manager(arguments[0]);
			std::vector<save::BackupLoadRequest> requests;
			if (arguments.size() == 2) {
				if (Vm.Type(arguments[1]) != SquirrelValueType::Array || Vm.Length(arguments[1]) > 65536) {
					throw std::runtime_error("Adventure backup load requires its bounded actual FileData array");
				}
				const auto count = Vm.Length(arguments[1]);
				requests.reserve(count);
				for (std::size_t index = 0; index < count; ++index) {
					const auto entry = Vm.ReadSlot(arguments[1], static_cast<std::int32_t>(index));
					if (Vm.Type(entry) != SquirrelValueType::Table) {
						throw std::runtime_error("Adventure backup FileData entry requires its actual table");
					}
					save::BackupLoadRequest request;
					request.SegmentId = static_cast<std::uint32_t>(index);
					if (Vm.HasSlot(entry, std::string("segmentId"))) {
						request.SegmentId = std::bit_cast<std::uint32_t>(BackupInteger(Vm.ReadSlot(entry, std::string("segmentId"))));
					}
					if (Vm.HasSlot(entry, std::string("fileId"))) {
						request.FileId = BackupInteger(Vm.ReadSlot(entry, std::string("fileId")));
					}
					if (Vm.HasSlot(entry, std::string("struct"))) {
						const auto target = Vm.ReadSlot(entry, std::string("struct"));
						if (!std::holds_alternative<std::monostate>(target)) {
							if (!Vm.IsInstanceOf(target, Structs.StructClass()) || request.SegmentId >= manager.Owner->SegmentCount() || &Structs.BorrowStructStorage(target) != &manager.Owner->Segment(request.SegmentId).Storage()) {
								throw std::runtime_error("Adventure backup FileData override requires its unrecovered distinct Struct/StructValue byte-range producer");
							}
						}
					}
					requests.push_back(request);
				}
			}

			manager.Owner->Load(requests);
		}

		void Install() {
			ManagerClass = Vm.CreateClass(Layers.LayerBaseClass());
			SegmentClass = Vm.CreateClass(Objects.ObjectClass());
			AdventureManagerClass = Vm.CreateClass(Layers.LayerBaseClass());
			AdventureSegmentClass = Vm.CreateClass(Objects.ObjectClass());
			Vm.SetClassTypeTag(ManagerClass, &ManagerTag);
			Vm.SetClassTypeTag(SegmentClass, &SegmentTag);
			Vm.SetClassTypeTag(AdventureManagerClass, &AdventureManagerTag);
			Vm.SetClassTypeTag(AdventureSegmentClass, &AdventureSegmentTag);
			ManagerClassRoot = Vm.Retain(ManagerClass);
			SegmentClassRoot = Vm.Retain(SegmentClass);
			AdventureManagerClassRoot = Vm.Retain(AdventureManagerClass);
			AdventureSegmentClassRoot = Vm.Retain(AdventureSegmentClass);
			const auto method = [this](const SquirrelValue& type, std::string_view name, auto operation) {
				const auto weak = Owner;
				const auto closure = Vm.CreateNativeClosure(std::string(name), [weak, operation](SquirrelVm&, auto arguments) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("Backup binding owner has expired");
					}

					return operation(*owner, arguments);
				});
				Vm.NewSlot(type, std::string(name), closure);
			};
			for (const auto profile : {save::BackupProfile::Ordinary, save::BackupProfile::Adventure}) {
				const auto& manager_class = profile == save::BackupProfile::Adventure ? AdventureManagerClass : ManagerClass;
				const auto& segment_class = profile == save::BackupProfile::Adventure ? AdventureSegmentClass : SegmentClass;
				method(manager_class, "constructor", [profile](Implementation& state, auto arguments) {
					state.ConstructManager(arguments, profile);

					return SquirrelNativeResult::NoValues();
				});
				method(segment_class, "constructor", [](Implementation&, auto) -> SquirrelNativeResult {
					throw std::runtime_error("can't create instance");
				});
				method(manager_class, "addSegment", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 3);
					auto& manager = state.Manager(arguments[0]);
					const auto count = BackupInteger(arguments[2]);
					if (count < 0 || manager.StructOwners.size() >= 1024) {
						throw std::runtime_error("Backup segment count exceeds its supported safe native domain");
					}
					auto& storage = state.Structs.BorrowStructStorage(arguments[1]);
					auto pin = state.Vm.Retain(arguments[1]);
					manager.StructOwners.reserve(manager.StructOwners.size() + 1);
					auto& segment = manager.Owner->AddSegment(storage, static_cast<std::uint32_t>(count));
					manager.StructOwners.push_back(std::move(pin));

					return SquirrelNativeResult::ReturnValue(state.WrapSegment(manager, segment.Ordinal()));
				});
				method(manager_class, "getSegment", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 2);

					return SquirrelNativeResult::ReturnValue(state.WrapSegment(state.Manager(arguments[0]), std::bit_cast<std::uint32_t>(BackupInteger(arguments[1]))));
				});
				method(manager_class, "init", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);
					state.Manager(arguments[0]).Owner->Initialize();

					return SquirrelNativeResult::NoValues();
				});
				method(manager_class, "getRunning", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);

					return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->Running());
				});
				method(manager_class, "getSuccess", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);

					return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->Success());
				});
				method(manager_class, "setTitle", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 2);
					state.Manager(arguments[0]).Owner->SetTitle(BackupString(arguments[1]));

					return SquirrelNativeResult::NoValues();
				});
				method(manager_class, "getTitle", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);

					return SquirrelNativeResult::ReturnValue(std::string(state.Manager(arguments[0]).Owner->Title()));
				});
				method(manager_class, "setSecureFileId", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 2);
					state.Manager(arguments[0]).Owner->SetSecureFileId(BackupString(arguments[1]));

					return SquirrelNativeResult::NoValues();
				});
				method(manager_class, "getSecureFileId", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);

					return SquirrelNativeResult::ReturnValue(std::string(state.Manager(arguments[0]).Owner->SecureFileId()));
				});
				method(manager_class, "setTitleId", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 2);
					static_cast<void>(state.Manager(arguments[0]));
					static_cast<void>(BackupString(arguments[1]));
					// Native Windows registration uses030f20's genuine void return.
					return SquirrelNativeResult::NoValues();
				});
				method(manager_class, "getTitleId", [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);
					static_cast<void>(state.Manager(arguments[0]));

					return SquirrelNativeResult::ReturnValue(std::string("00000000"));
				});
				InstallSegmentMethods(method, segment_class);
			}
			method(AdventureSegmentClass, "getSegmentId", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(state.SegmentOwner(arguments[0]).Ordinal()));
			});
			method(AdventureManagerClass, "getInitSuccess", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->InitSuccess());
			});
			method(AdventureManagerClass, "setEnableMetaData", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.Manager(arguments[0]).Owner->SetEnableMetaData(BackupBoolean(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(AdventureManagerClass, "getEnableMetaData", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->EnableMetaData());
			});
			method(AdventureManagerClass, "isExistTitleSavedata", [](Implementation& state, auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("invalid argument.");
				}
				static_cast<void>(state.Manager(arguments[0]));
				// The actual Windows registration dispatches04b570's constant false.
				return SquirrelNativeResult::ReturnValue(false);
			});
			method(AdventureManagerClass, "autoload", [](Implementation& state, auto arguments) {
				state.LoadAdventure(arguments);

				return SquirrelNativeResult::NoValues();
			});
			for (const auto name : {"autosave"}) {
				method(AdventureManagerClass, name, [name](Implementation& state, auto arguments) -> SquirrelNativeResult {
					if (arguments.empty()) {
						throw std::runtime_error("invalid argument.");
					}
					static_cast<void>(state.Manager(arguments[0]));
					throw std::runtime_error(std::string("AdvBackupManager.") + name + " requires its actual recovered batch/metadata file producer");
				});
			}
			method(AdventureManagerClass, "getNospace", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->NoSpace());
			});
			method(AdventureManagerClass, "getBroken", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->Broken());
			});
			method(AdventureManagerClass, "getNofile", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->NoFile());
			});
			method(AdventureManagerClass, "getOffline", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Manager(arguments[0]).Owner->Offline());
			});
			Vm.NewSlot(Vm.RootTable(), std::string("AdvBackupManager"), AdventureManagerClass);
			Vm.NewSlot(Vm.RootTable(), std::string("AdvBackupSegment"), AdventureSegmentClass);
			Vm.NewSlot(Vm.RootTable(), std::string("BackupManager"), ManagerClass);
			Vm.NewSlot(Vm.RootTable(), std::string("BackupSegment"), SegmentClass);
		}

		template <typename Method>
		void InstallSegmentMethods(const Method& method, const SquirrelValue& segment_class) {
			method(segment_class, "getRunning", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);
				const auto& segment = state.Segment(arguments[0]);

				return SquirrelNativeResult::ReturnValue(state.SegmentManager(segment).Running());
			});
			method(segment_class, "getSuccess", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Success());
			});
			method(segment_class, "getMaxFileNum", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(state.SegmentOwner(arguments[0]).MaxFileCount()));
			});
			method(segment_class, "setFileId", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.SegmentOwner(arguments[0]).SetFileId(BackupInteger(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			for (const auto name : {"getFileId", "getFileIdLastOperate"}) {
				method(segment_class, name, [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);

					return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Metadata().FileId);
				});
			}
			method(segment_class, "setRecordFileId", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.SegmentOwner(arguments[0]).SetRecordFileId(BackupBoolean(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "getRecordFileId", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Metadata().RecordFileId);
			});
			method(segment_class, "setDialogName", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				auto& segment = state.Segment(arguments[0]);
				const auto text = BackupString(arguments[1]);
				if (text.size() > 1024 * 1024) {
					throw std::runtime_error("Backup dialog name exceeds its bounded string domain");
				}
				segment.DialogName = text;

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "getDialogName", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.Segment(arguments[0]).DialogName);
			});
			method(segment_class, "setComment", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.SegmentOwner(arguments[0]).SetComment(BackupString(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "getComment", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Metadata().Comment);
			});
			method(segment_class, "setDetail", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.SegmentOwner(arguments[0]).SetDetail(BackupString(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "getDetail", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Metadata().Detail);
			});
			for (const auto name : {"setNoDuplicate", "setNoCheckOwner"}) {
				method(segment_class, name, [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 2);
					static_cast<void>(state.SegmentOwner(arguments[0]));
					// Native030f20 is the Windows profile's genuine empty setter.
					return SquirrelNativeResult::NoValues();
				});
			}
			for (const auto name : {"getNoDuplicate", "getNoCheckOwner", "getNoMedia", "getCanceled"}) {
				method(segment_class, name, [](Implementation& state, auto arguments) {
					RequireBackupArguments(arguments, 1);
					static_cast<void>(state.SegmentOwner(arguments[0]));

					return SquirrelNativeResult::ReturnValue(false);
				});
			}
			method(segment_class, "getCrypted", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);
				const auto& segment = state.Segment(arguments[0]);
				auto& manager = state.SegmentManager(segment);

				return SquirrelNativeResult::ReturnValue(manager.Profile() == save::BackupProfile::Adventure ? manager.Segment(segment.Ordinal).Crypted() : false);
			});
			method(segment_class, "setNoConfirm", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.SegmentOwner(arguments[0]).SetNoConfirm(BackupBoolean(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "getNoConfirm", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Metadata().NoConfirm);
			});
			method(segment_class, "setNoticeComplete", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 2);
				state.SegmentOwner(arguments[0]).SetNoticeComplete(BackupBoolean(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "getNoticeComplete", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Metadata().NoticeComplete);
			});
			method(segment_class, "autoload", [](Implementation& state, auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("bad instance");
				}
				const auto segment = state.Segment(arguments[0]);
				auto& manager = state.SegmentManager(segment);
				if (manager.Profile() != save::BackupProfile::Adventure) {
					throw std::runtime_error("BackupSegment.autoload requires its unrecovered ordinary file producer");
				}
				// Native0793a0/076570 append a BelowNormal segment task to the actual manager camera set.
				manager.LoadSegment(segment.Ordinal);

				return SquirrelNativeResult::NoValues();
			});
			method(segment_class, "autosave", [](Implementation& state, auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("bad instance");
				}
				const auto segment = state.Segment(arguments[0]);
				auto& manager = state.SegmentManager(segment);
				if (manager.Profile() != save::BackupProfile::Adventure) {
					throw std::runtime_error("BackupSegment.autosave requires its unrecovered ordinary file producer");
				}
				// Native0794f0/076640 append the distinct075a40 segment-save worker.
				manager.SaveSegment(segment.Ordinal);

				return SquirrelNativeResult::NoValues();
			});
			for (const auto name : {"singleload", "singlesave", "autodelete", "singledelete", "listload", "listsave", "listdelete", "hostload", "hostsave", "setIcon", "setListIconResource", "getListIconResource", "setListMotionResource", "getListMotionResource", "setListFontResource", "getListFontResource", "setListAutoClose", "getListAutoClose"}) {
				method(segment_class, name, [name](Implementation& state, auto arguments) -> SquirrelNativeResult {
					if (arguments.empty()) {
						throw std::runtime_error("invalid argument.");
					}
					static_cast<void>(state.SegmentOwner(arguments[0]));
					throw std::runtime_error(std::string("BackupSegment.") + name + " reached its native file/dialog/resource producer, which remains unrecovered");
				});
			}
			method(segment_class, "getNospace", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).NoSpace());
			});
			method(segment_class, "getBroken", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Broken());
			});
			method(segment_class, "getNofile", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).NoFile());
			});
			method(segment_class, "getOffline", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(state.SegmentOwner(arguments[0]).Offline());
			});
			method(segment_class, "existentcheck", [](Implementation& state, auto arguments) {
				RequireBackupArguments(arguments, 1);
				static_cast<void>(state.SegmentOwner(arguments[0]));
				// Windows registration also uses030f20 for this void operation.
				return SquirrelNativeResult::NoValues();
			});
		}
	};

	ScriptBackupBindings::ScriptBackupBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptStructBindings& structs, core::BackgroundCallManager& background, std::function<bool()> initialize_directory)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, structs, background, std::move(initialize_directory))) {
		m_Implementation->Owner = m_Implementation;
		m_Implementation->Install();
	}

	ScriptBackupBindings::~ScriptBackupBindings() = default;

	void ScriptBackupBindings::ConfigureFiles(save::BackupFileServices services) {
		auto& state = *m_Implementation;
		if (state.Files || state.ManagerConstructionStarted || services.Directory.empty() || !services.Decrypt) {
			throw std::runtime_error("Backup files require one real directory/codec bundle before native manager construction");
		}

		state.Files = std::move(services);
	}

	SquirrelValue ScriptBackupBindings::ManagerClass() const {
		return m_Implementation->ManagerClass;
	}

	SquirrelValue ScriptBackupBindings::SegmentClass() const {
		return m_Implementation->SegmentClass;
	}

	SquirrelValue ScriptBackupBindings::AdventureManagerClass() const {
		return m_Implementation->AdventureManagerClass;
	}

	SquirrelValue ScriptBackupBindings::AdventureSegmentClass() const {
		return m_Implementation->AdventureSegmentClass;
	}

} // namespace sgre::script
