# Borrowed Layer views

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE view; reference bytes and analysis metadata remain unchanged.

The common SQLayerView constructor `0x14022c5c0` borrows its actual motion Layer and appends its pointer to that Layer's ordered vector through `0x14022bce0`. Its destructor `0x14022cac0` stably unlinks the first matching pointer unless the parent already marked it detached. Registration alone does not apply font filtering; the Indicator constructor applies the initial value separately after its font/task exists.

Smoothing `0x140234540` first stores the common Layer flag, then visits surviving views in registration order, then filters the primary and secondary source managers. MotionLayerBase teardown `0x14022cb60` marks every remaining view detached before clearing their borrowed vector. Concrete Layer task/resource cleanup precedes that base teardown. The authored registry connects this measured portion to current paired motion-resource cleanup; full native task-owner teardown remains open.

`ScriptLayerBindings::BorrowedViewRegistration` is a move-only RAII token. It stores a weak adapter and generation-checked nonowning script identity, never a strong parent value. Normal reset unlinks without a detach callback. The registered callbacks borrow external view state: reset/destroy the token before that state, keep adapters/services alive through VM Close, and do not capture strong script instances. Parent release closes current resource owners before detaching surviving views; failures still retire all views and parent state before reporting the first exception.

Authored safety limits registration to 65,536 views, guards identifier exhaustion and rejects registration, recursive smoothing or parent release during traversal/retirement. Token destruction during smoothing marks an entry retired and defers erasure until its callback returns. These mutation guards are safety policies, not claims about native callback reentry.

A private original-instruction oracle covers 1,000 cases and 41,000 register/remove/smoothing/detach operations with 2,436,216 instructions and zero differences. Semantic SHA-256: `0b33013b6a72ac0a91399482fb736e8cb23744862bd4715adb0fd4bcdf8527eb`. Object construction/destruction, the LayerBase tail destructor, allocation/release and view/source-manager filter observers are substituted boundaries. The complete view state, survivor order and manager callback ordering are compared; no game process, window or audio output is used.

Independent sanitizer fixtures cover move-only transfer, stable unlink, true-reclaimer weak-parent retirement with real BitmapIndicator detachment, removal during traversal and failure-completing retirement. Existing actual paired-resource tests also observe view filtering before texture sampler changes and GPU/PSB cleanup before detachment. The host invocation return cell is explicitly cleared before testing last-caller-value retirement, because it separately retains a VM result.

This registry is not an Indicator script class, font provider, renderer or task implementation. Actual LayerView/Indicator constructor bindings, font/texture ownership and scene submission remain the next integration checks.
