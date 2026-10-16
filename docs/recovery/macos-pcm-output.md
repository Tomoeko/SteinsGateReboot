# macOS PCM output

Source-only candidate; not compiled, enabled or executed.

This is an authored platform transport, not a recovered XAudio2 device implementation. Existing first-party decoders, cue ownership, matrix/gain/pitch services and actual manual mixer remain the producers. No compressed data reaches the hardware callback.

Normal visible launches explicitly create Apple's DefaultOutput AudioUnit, configure its input as interleaved native-endian Float32 PCM, prefill the owned ring and call Start. Headless/legacy research/validate-only paths never create this owner and retain the original discard loop. The DefaultOutput unit selects the user's default device; the app does not change the device, speaker volume, channel mapping or nominal hardware rate. Its actual client format/device identity/output format are checked on the producer thread. Apple documents the output unit's PCM conversion and default channel mapping; cross-device/sample-rate/filter parity is unmeasured.

The ring has one producer on the existing application owner thread and one serialized device consumer. Construction allocates all sample storage. Acquire/release frame cursors fence whole-frame publication and reuse. Full writes fail without partial publication; starvation zero-fills only absent frames without advancing the source. Finish preserves its queued software tail. Software drain is distinct from hardware/DAC latency. The device callback performs bounded copies/fills and lock-free atomic observations only. It invokes no decoder, mixer, script, scene, Objective-C, property getter, logger, allocator, wait or lock. Invalid buffer geometry/reentrant callbacks latch an error for the owner. Property listeners only publish an atomic notification; owner-thread service checks route/format/render errors.

Visible hosting initially admits the existing48k stereo Sound/Movie domain. Each owner phase snapshots how many frames reach the4096-frame target in an8192-frame ring, then renders at most that fixed amount in existing bounded chunks. At48k this is approximately85.3ms of target PCM lead; it is an explicit port buffering choice, not an original latency claim. Slow owner phases may starve; counters expose that instead of moving decoder work into the device callback. Movie RenderPcm reads the same actual manual mix, preserving pending/assembled/submitted buffers, pause semantics, clock/gain and close ordering. The owner sums Sound and Movie PCM before submission. No sink-level gain/clamp/mute is added.

Existing defaults are enabled: SoundResourceControl master1, absent group1, SoundMixParameters voice/resource/group/archive/master/mute multipliers1. Script-controlled master/group/voice settings and loaded user choices remain authoritative. Sound.setMute/getMute are still independently gated in current bindings; the transport does not pretend those methods are recovered. The host never forces gain1 during frames.

Close disables sample consumption, stops/uninitializes the unit, removes property/render callbacks, disposes it and requires callback quiescence before freeing storage. All potentially blocking lifecycle work stays on the producer thread. Cleanup attempts disposal even when an earlier cleanup operation fails, retains the first status, and never destroys a refcon while a failed unit disposal may still own it. Destruction cannot throw.

Acceptance remains open: strict compilation; portable ring/SPSC and direct production-callback CPU fixtures; unchanged movie tests plus actual offline PCM readout fixture; coherent host integration; actual authorized device startup/hotplug/shutdown/performance and audible acceptance. Author executed no build, AudioUnit operation, device query/start, window, game or audio playback. Quiet fixtures create no AudioUnit; movie readout uses the established offline-only AVAudioEngine boundary. Live counter fields are separate atomic observations, not one coherent cross-field sample.

Primary Apple references:

- [Core Audio common tasks](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/ARoadmaptoCommonTasks/ARoadmaptoCommonTasks.html): output-unit PCM conversion/channel mapping and default-vs-system output.
- [System-supplied units](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/SystemAudioUnits/SystemAudioUnits.html): DefaultOutput versus GenericOutput.
- [Audio Unit architecture](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnit/TheAudioUnit.html): input-scope format and render callback connection.
- [QA1715](https://developer.apple.com/library/archive/qa/qa1715/): real-time callback restrictions. This is a RemoteIO note; the no-block/no-allocation rule is applied conservatively to this macOS consumer.
- [Building a signal generator](https://developer.apple.com/documentation/avfaudio/building-a-signal-generator): current real-time render restrictions.
- Installed Apple SDK AudioUnitProperties.h: SetRenderCallback Input scope/zero-fill, MaximumFramesPerSlice and device property contracts; AUComponent.h lifecycle order. Exact header hashes are private receiving evidence.

A target must cover the configured maximum callback. The owner reads back MaximumFramesPerSlice after initialization and after notified property changes, rejecting any zero, wrong extent or unsupported growth before further service. Apple QA1533 documents converter-driven growth and multiple input pulls; this finite target does not guarantee arbitrary multi-pull bursts or owner scheduling latency. Owner-side route/format/bound errors remain sticky across diagnostic Snapshot calls. Quiet CPU fixtures exercise repeated4096-frame pulls/refills only; no hardware has been started.

Primary reference: [Apple QA1533](https://developer.apple.com/library/archive/qa/qa1533/_index.html).
