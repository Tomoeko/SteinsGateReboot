cmake_minimum_required(VERSION 3.24)
project(SteinsGateReboot VERSION 0.1.0 LANGUAGES CXX)
find_package(Threads REQUIRED)

option(SGRE_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(SGRE_BUILD_TESTS "Build independent resource recovery tests" ON)
option(SGRE_BUILD_GAME "Build the native game application" ${APPLE})
option(SGRE_BUILD_MACOS_MEDIA "Build the native macOS media boundary" ${APPLE})
option(SGRE_BUILD_MACOS_RENDERER "Build the native macOS renderer boundary" ${APPLE})
set(SGRE_GAME_ROOT "" CACHE PATH "Optional local reference installation for integration checks")
set(SGRE_SEED_FILE "" CACHE FILEPATH "Local recovered archive seed for integration checks")
option(BUILD_SHARED_LIBS "Link the first-party core once across recovery executables" OFF)

add_library(sgre_resource
	src/app/launch_arguments.cpp
	src/app/display_path.cpp
	src/app/launch_configuration.cpp
	src/app/launch_configuration_json.cpp
	src/app/launch_archive_seed.cpp
	src/audio/pcm_output_ring.cpp
	src/audio/pcm_output_delivery.cpp
	src/audio/pcm_output_submission.cpp
	src/audio/sound_configuration_loads.cpp
	src/audio/sound_resource_control.cpp
	src/audio/sound_playback.cpp
	src/audio/buffered_sound_source_data.cpp
	src/audio/buffered_sound_sources.cpp
	src/audio/buffered_sound_cues.cpp
	src/audio/sound_volume.cpp
	src/audio/sound_volume_animation.cpp
	src/audio/sound_volume_animations.cpp
	src/audio/voice_schedule.cpp
	src/audio/voice_allocation.cpp
	src/audio/voice_uniqueness.cpp
	src/audio/wave_decoder.cpp
	src/audio/wma_packet.cpp
	src/audio/wma_stream.cpp
	src/audio/wma_exponent.cpp
	src/audio/wma_scaling.cpp
	src/audio/wma_transform.cpp
	src/audio/wma_window.cpp
	src/audio/wma_block.cpp
	src/audio/wma_coefficient.cpp
	src/audio/wma_codebooks.cpp
	src/audio/wma_decoder.cpp
	src/audio/wma_stream_decoder.cpp
	src/audio/wma_stream_source.cpp
	src/core/frame_clock.cpp
	src/core/md5.cpp
	src/core/thread_task_frame.cpp
	src/core/looper_frame.cpp
	src/core/application_event_control.cpp
	src/core/window_frame_pacing.cpp
	src/core/application_clock_pump.cpp
	src/core/performance_counter.cpp
	src/core/background_calls.cpp
	src/core/locale.cpp
	src/input/device.cpp
	src/input/hub.cpp
	src/input/key_mapping.cpp
	src/input/function_keys.cpp
	src/input/keyboard_text.cpp
	src/input/frame_sample.cpp
	src/input/mouse.cpp
	src/io/file_reader.cpp
	src/scenario/scenario_search.cpp
	src/scenario/calendar_restore.cpp
	src/io/file_read_serial.cpp
	src/io/raw_read_destination.cpp
	src/io/persistent_raw_queue.cpp
	src/io/pending_raw_reads.cpp
	src/io/file_read_processor.cpp
	src/io/script_read_buffer.cpp
	src/io/script_buffer_reader.cpp
	src/io/resource_file_read.cpp
	src/io/mounted_resource_read.cpp
	src/io/wrapped_psb_destination.cpp
	src/media/webm.cpp
	src/media/movie_gain.cpp
	src/media/opus.cpp
	src/scene/movie_packet.cpp
	src/scene/movie_layer_task.cpp
	src/script/layer_movie_bindings.cpp
	src/resource/archive_index.cpp
	src/resource/archive_mounts.cpp
	src/resource/archive_reader.cpp
	src/resource/font.cpp
	src/resource/resident_font.cpp
	src/resource/sfnt.cpp
	src/resource/truetype_outline.cpp
	src/text/truetype_glyph_font.cpp
	src/text/truetype_hinted_glyph_font.cpp
	src/text/truetype_glyph_path.cpp
	src/text/truetype_program.cpp
	src/text/truetype_stack_program.cpp
	src/resource/opentype_layout.cpp
	src/resource/cff.cpp
	src/resource/cff_driver_hints.cpp
	src/resource/cff_outline.cpp
	src/resource/cff_geometry.cpp
	src/resource/image.cpp
	src/resource/mzs_shell.cpp
	src/resource/motion.cpp
	src/resource/motion_mesh.cpp
	src/resource/psb.cpp
	src/resource/psb_jobs.cpp
	src/resource/resource_cipher.cpp
	src/resource/resource_cache.cpp
	src/resource/resource_manager_process.cpp
	src/resource/sound.cpp
	src/resource/sound_definitions.cpp
	src/resource/struct_storage.cpp
	src/resource/zstd_decoder.cpp
	src/scene/image_assets.cpp
	src/scene/image_receiving_document.cpp
	src/scene/image_preparation_service.cpp
	src/scene/motion_assets.cpp
	src/scene/motion_receiving_document.cpp
	src/scene/motion_preparation_service.cpp
	src/scene/motion_batch_flush.cpp
	src/scene/motion_clip_bounds.cpp
	src/scene/motion_division.cpp
	src/scene/motion_timeline_clock.cpp
	src/scene/motion_timeline_variables.cpp
	src/scene/motion_player_commands.cpp
	src/scene/motion_clone_resource.cpp
	src/scene/motion_scene_clone_state.cpp
	src/scene/motion_layer_uids.cpp
	src/scene/motion_player_update.cpp
	src/scene/motion_child_initial_timing.cpp
	src/scene/motion_child_control.cpp
	src/scene/motion_child_configuration.cpp
	src/scene/motion_child_construction.cpp
	src/scene/motion_child_frame_slots.cpp
	src/scene/motion_child_frame_selection.cpp
	src/scene/motion_layer_identifiers.cpp
	src/scene/motion_selected_layer_nodes.cpp
	src/scene/motion_selected_layer_sources.cpp
	src/scene/motion_stencil_mask_owner.cpp
	src/scene/motion_particle_definition.cpp
	src/scene/motion_particle_frame_slots.cpp
	src/scene/motion_particle_emission.cpp
	src/scene/motion_particle_player_state.cpp
	src/scene/motion_particle_birth.cpp
	src/scene/motion_particle_retirement.cpp
	src/scene/motion_particle_layer_state.cpp
	src/scene/motion_layer_stencil_ancestry.cpp
	src/scene/motion_stencil_row_policies.cpp
	src/scene/motion_selected_quad_geometry.cpp
	src/scene/motion_selected_mask_packets.cpp
	src/scene/motion_selected_sprite_player.cpp
	src/scene/motion_manager_runtime.cpp
	src/scene/motion_player_task.cpp
	src/scene/motion_sprite_draw_context.cpp
	src/scene/motion_child_root.cpp
	src/scene/motion_child_direction.cpp
	src/scene/motion_child_direction_phase.cpp
	src/scene/motion_child_target.cpp
	src/scene/motion_direction_math.cpp
	src/scene/motion_random.cpp
	src/scene/motion_seed_buffer.cpp
	src/scene/motion_evaluator.cpp
	src/scene/layers.cpp
	src/scene/layer_manager_frame.cpp
	src/scene/layer_folder_frame.cpp
	src/scene/default_driver_scene.cpp
	src/scene/motion_layer_frame.cpp
	src/scene/motion_root_scene.cpp
	src/scene/motion_layer_composition.cpp
	src/scene/motion_layer_parameters.cpp
	src/scene/motion_layer_clip_rectangles.cpp
	src/scene/motion_priority_cursor.cpp
	src/scene/motion_local_frames.cpp
	src/scene/motion_selected_mesh_geometry.cpp
	src/scene/motion_selected_mesh_ancestry.cpp
	src/scene/motion_selected_mesh_vertices.cpp
	src/render/presentation_rectangle.cpp
	src/render/frame_host_heap.cpp
	src/render/frame_statistics.cpp
	src/render/graphics_default_state.cpp
	src/render/sprite_host_inputs.cpp
	src/scene/motion_selected_mesh_mask_packets.cpp
	src/scene/motion_layer_mesh_local_owner.cpp
	src/scene/motion_static_source.cpp
	src/scene/motion_blank_source.cpp
	src/scene/motion_parameter_range.cpp
	src/scene/motion_parameter_values.cpp
	src/scene/motion_variable_animations.cpp
	src/text/combining_reorder.cpp
	src/text/nominal_glyph.cpp
	src/text/variation_glyph.cpp
	src/text/recursive_decomposition.cpp
	src/text/unicode_buffer_input.cpp
	src/text/unicode_buffer_positions.cpp
	src/text/neutral_glyph_preparation.cpp
	src/text/initial_dotted_circle.cpp
	src/text/glyph_definition_blob.cpp
	src/text/glyph_definition_entry.cpp
	src/text/glyph_substitution_blob.cpp
	src/text/glyph_positioning_blob.cpp
	src/text/layout_lookup_accelerator.cpp
	src/text/blob_font_face.cpp
	src/text/blob_font_layout_cache.cpp
	src/scene/common_process_tasks.cpp
	src/scene/looper_hook_tasks.cpp
	src/scene/resource_meter_process.cpp
	src/scene/affine_layer_parameters.cpp
	src/scene/affine_layer_order_workspace.cpp
	src/scene/affine_atan2.cpp
	src/scene/affine_layer_order.cpp
	src/scene/affine_matrix.cpp
	src/scene/affine_matrix_geometry.cpp
	src/scene/picture_regions.cpp
	src/scene/picture_layer_task.cpp
	src/scene/picture_packet.cpp
	src/scene/vertex_modulator.cpp
	src/scene/affine_plane_transform.cpp
	src/scene/affine_trigonometry.cpp
	src/scene/indicator_placement.cpp
	src/scene/bitmap_indicator.cpp
	src/scene/bitmap_font_assets.cpp
	src/scene/bitmap_font_receiving_document.cpp
	src/scene/bitmap_font_preparation_service.cpp
	src/scene/parallel_task_set.cpp
	src/scene/serial_task_set.cpp
	src/scene/motion_layer_tasks.cpp
	src/scene/layer_scale_view.cpp
	src/scene/layer_camera_draw.cpp
	src/scene/layer_transform.cpp
	src/scene/screens.cpp
	src/scene/render_target_layout.cpp
	src/scene/default_render_model.cpp
	src/scene/graphics_model_stack.cpp
	src/scene/motion_deformation.cpp
	src/scene/motion_bezier_lattice.cpp
	src/scene/motion_bezier_scale.cpp
	src/scene/motion_affine_decomposition.cpp
	src/scene/motion_bezier_inverse.cpp
	src/scene/motion_deformation_chain.cpp
	src/scene/motion_geometry.cpp
	src/scene/motion_tessellation.cpp
	src/scene/motion_source_registry.cpp
	src/scene/motion_projects.cpp
	src/scene/motion_quad_batch.cpp
	src/scene/motion_render_queue.cpp
	src/scene/motion_mask_plan.cpp
	src/scene/motion_frame_submission.cpp
	src/scene/motion_masked_blend.cpp
	src/scene/motion_stencil_state.cpp
	src/scene/motion_shader_program.cpp
	src/scene/motion_copy_target.cpp
	src/scene/motion_copied_target_owner.cpp
	src/scene/motion_copied_region_owner.cpp
	src/scene/motion_texture_binding.cpp
	src/scene/motion_copy_region.cpp
	src/scene/motion_alternate_program.cpp
	src/scene/raw_texture_color.cpp
	src/scene/raw_image.cpp
	src/scene/mutable_texture_owner.cpp
	src/scene/raw_texture_packet.cpp
	src/scene/raw_texture_restore.cpp
	src/scene/raw_texture_task.cpp
	src/scene/raw_layer_owner.cpp
	src/scene/motion_mask_shader.cpp
	src/scene/motion_scissor.cpp
	src/scene/motion_resources.cpp
	src/scene/motion_textures.cpp
	src/scene/motion_shared_textures.cpp
	src/scene/native_texture_reference.cpp
	src/scene/motion_alternate_textures.cpp
	src/scene/motion_draw.cpp
	src/scene/motion_grid_draw.cpp
	src/scene/sprite_pipeline.cpp
	src/scene/texture_cache.cpp
	src/script/squirrel_bytecode.cpp
	src/script/affine_matrix_bindings.cpp
	src/script/affine_layer_bindings.cpp
	src/script/affine_layer_links.cpp
	src/script/affine_layer_matrices.cpp
	src/script/binary_bindings.cpp
	src/script/game_bindings.cpp
	src/script/input_bindings.cpp
	src/script/indicator_bindings.cpp
	src/script/font_bindings.cpp
	src/script/raw_image_bindings.cpp
	src/script/raw_texture_bindings.cpp
	src/script/layer_raw_texture_bindings.cpp
	src/script/draw_object_bindings.cpp
	src/script/layer_draw_bindings.cpp
	src/script/layer_picture_bindings.cpp
	src/script/layer_proxy_bindings.cpp
	src/script/fill_rectangle_bindings.cpp
	src/scene/fill_rectangle_task.cpp
	src/scene/capture_task.cpp
	src/script/capture_bindings.cpp
	src/script/captured_bindings.cpp
	src/scene/captured_task.cpp
	src/scene/captured_opacity_animation.cpp
	src/scene/captured_opacity_map.cpp
	src/script/vector_font_bindings.cpp
	src/script/text_font_info.cpp
	src/script/text_font_owner.cpp
	src/script/text_render_bindings.cpp
	src/script/image_bindings.cpp
	src/script/layer_bindings.cpp
	src/script/screen_bindings.cpp
	src/script/native_format.cpp
	src/script/driver_logs.cpp
	src/script/debug_scene_entry.cpp
	src/script/original_checkpoint.cpp
	src/save/scene_checkpoint_cache.cpp
	src/script/original_calendar_restore.cpp
	src/script/original_scene_seek.cpp
	src/script/driver_phases.cpp
	src/script/native_float.cpp
	src/script/native_math.cpp
	src/script/random_bindings.cpp
	src/script/packed_color_bindings.cpp
	src/script/motion_resource_owner.cpp
	src/script/motion_native_owners.cpp
	src/script/motion_bindings.cpp
	src/script/object_bindings.cpp
	src/script/emote_bindings.cpp
	src/script/psb_bindings.cpp
	src/script/struct_bindings.cpp
	src/script/resource_bindings.cpp
	src/script/resource_file_service.cpp
	src/script/sound_bindings.cpp
	src/script/application_finish_bindings.cpp
	src/script/script_buffer_load.cpp
	src/script/resource_request_identity.cpp
	src/script/script_scheduler.cpp
	src/script/prepared_driver_close.cpp
	src/script/thread_bindings.cpp
	src/script/system_bindings.cpp
	src/script/squirrel_vm.cpp
	src/script/template_strings.cpp
	src/script/source_expression.cpp
	src/script/source_compiler.cpp
	src/script/motion_layer_getter_bindings.cpp
	src/script/motion_layer_setter_bindings.cpp
	src/script/motion_root_coordinate_bindings.cpp
	src/script/motion_variable_bindings.cpp
	src/resource/emote_selectors.cpp
	src/scene/emote_selector_control.cpp
	src/scene/emote_variable_controls.cpp
	src/scene/emote_timeline_tracks.cpp
	src/scene/emote_timeline_scheduler.cpp
	src/scene/emote_color_transition.cpp
	src/scene/emote_coordinate_transition.cpp
	src/scene/emote_scale_transition.cpp
	src/scene/emote_rotation_transition.cpp
	src/scene/emote_wind.cpp
	src/scene/emote_timeline_transition.cpp
	src/scene/emote_soft_pendulum.cpp
	src/scene/emote_catenary_curve.cpp
	src/scene/emote_catenary_physics.cpp
	src/scene/emote_catenary_definition.cpp
	src/scene/emote_catenary_control.cpp
	src/scene/emote_bobble_model.cpp
	src/scene/emote_bobble_sections.cpp
	src/scene/emote_jelly_profile.cpp
	src/scene/emote_jelly_sample_fit.cpp
	src/scene/emote_jelly_section_rings.cpp
	src/scene/emote_jelly_surface_samples.cpp
	src/scene/emote_jelly_surface_bindings.cpp
	src/scene/emote_physical_math.cpp
	src/scene/emote_easing.cpp
	src/scene/emote_jelly_physics.cpp
	src/scene/emote_jelly_definition.cpp
	src/scene/emote_jelly_control.cpp
	src/scene/emote_tress_model.cpp
	src/scene/emote_tress_spline.cpp
	src/scene/emote_string_definition.cpp
	src/scene/emote_string_physics.cpp
	src/scene/emote_string_mesh_output.cpp
	src/scene/emote_string_control.cpp
	src/scene/emote_synchronize_control.cpp
	src/scene/emote_stretch_control.cpp
	src/scene/emote_parts_tree.cpp
	src/scene/emote_collider.cpp
	src/scene/emote_ellipse_collision.cpp
	src/scene/emote_emission_pool.cpp
	src/scene/emote_emission_emitter.cpp
	src/scene/emote_emission_definition.cpp
	src/scene/emote_emission_owner.cpp
	src/scene/emote_velvety_owner.cpp
	src/scene/emote_scene_player.cpp
	src/scene/emote_player_task.cpp
	src/script/emote_player_bindings.cpp
	src/scene/emote_bloom_physics.cpp
	src/scene/emote_bloom_definition.cpp
	src/scene/emote_bloom_control.cpp
	src/scene/emote_collision_owner.cpp
	src/scene/emote_rigid_region.cpp
	src/scene/emote_mesh_fix_alignment.cpp
	src/scene/emote_velvety_posture.cpp
	src/scene/emote_velvety_frame.cpp
	src/scene/emote_point_prediction.cpp
	src/scene/emote_graph_control.cpp
	src/scene/emote_blink_control.cpp
	src/scene/emote_clamp_control.cpp
	src/scene/screen_projection_packet.cpp
	src/scene/emote_variable_program.cpp
	src/text/bitmap_draw.cpp
	src/text/block_geometry.cpp
	src/text/cff_blue_zones.cpp
	src/text/cff_glyph_metrics.cpp
	src/text/cff_hinted_metrics.cpp
	src/text/cff_glyph_font.cpp
	src/text/cff_glyph_atlas.cpp
	src/text/cff_glyph_path.cpp
	src/text/cff_glyph_stroke.cpp
	src/text/cff_glyph_stroke_curve.cpp
	src/text/cff_glyph_stroke_path.cpp
	src/text/cff_hint_map.cpp
	src/text/cff_hint_geometry.cpp
	src/text/cff_contour_junction.cpp
	src/text/cff_pending_contour.cpp
	src/text/cff_contour_owner.cpp
	src/text/cff_contour_outline.cpp
	src/text/cff_default_hinting.cpp
	src/text/cff_hinted_charstring.cpp
	src/text/cff_stem_map.cpp
	src/text/cff_charstring_mask.cpp
	src/text/cff_stem_declarations.cpp
	src/text/element_draw.cpp
	src/text/encoding.cpp
	src/text/layout.cpp
	src/text/indicator_text.cpp
	src/text/glyph_run.cpp
	src/text/glyph_atlas.cpp
	src/text/glyph_atlas_owner.cpp
	src/text/render_context.cpp
	src/text/content_owner.cpp
	src/text/glyph_raster_edges.cpp
	src/text/glyph_raster_cells.cpp
	src/text/glyph_raster_solid_copy.cpp
	src/text/line_geometry.cpp
	src/text/wrapping.cpp
	src/text/markup.cpp
	src/text/reveal.cpp
	src/text/render_plan.cpp
	src/text/vector_metrics.cpp
	src/text/vector_font.cpp
	src/text/vector_font_run.cpp
	src/text/vector_font_unit.cpp
	src/text/vector_font_metric_services.cpp
	src/text/vector_font_normalization_font.cpp
	src/text/vector_font_shape_plan.cpp
	src/text/vector_font_neutral_entry.cpp
	src/text/vector_glyph_cache.cpp
	src/text/vector_glyph_draw.cpp
	src/text/font_selection.cpp
	src/text/shaping_font_map.cpp
	src/text/single_substitution.cpp
	src/text/unicode_preprocess.cpp
	src/text/initial_clusters.cpp
	src/text/direction_adjustment.cpp
	src/text/unicode_properties.cpp
	src/text/unicode_mirroring.cpp
	src/text/unicode_decomposition.cpp
	src/text/unicode_composition.cpp
	src/text/shaping_lookup_plan.cpp
	src/text/shaping_features.cpp
	src/text/shaping_feature_map.cpp
	src/text/formatting.cpp
	src/text/face_names.cpp
	src/text/property_settings.cpp
	src/text/processing_options.cpp
)
target_include_directories(sgre_resource PUBLIC include)
target_compile_features(sgre_resource PUBLIC cxx_std_20)
set_target_properties(sgre_resource PROPERTIES CXX_EXTENSIONS OFF)

add_library(sgre_build_options INTERFACE)
if(MSVC)
	target_compile_options(sgre_build_options INTERFACE /W4 /permissive-)
else()
	target_compile_options(sgre_build_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -ffp-contract=off)
endif()

if(SGRE_ENABLE_SANITIZERS)
	if(MSVC)
		message(FATAL_ERROR "This sanitizer configuration requires Clang or GCC")
	endif()

	target_compile_options(sgre_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(sgre_build_options INTERFACE -fsanitize=address,undefined)
endif()

target_link_libraries(sgre_resource PUBLIC sgre_build_options Threads::Threads)

if(APPLE)
	# Omit unused functions in static and shared validation builds to bound storage.
	target_link_options(sgre_build_options INTERFACE -Wl,-dead_strip)
endif()

if(APPLE OR SGRE_BUILD_MACOS_MEDIA OR SGRE_BUILD_MACOS_RENDERER)
	if(NOT APPLE)
		message(FATAL_ERROR "Native macOS boundaries require an Apple platform")
	endif()

	enable_language(OBJCXX)
endif()

if(APPLE)
	add_library(sgre_platform_save STATIC src/platform/macos/backup_crypto.mm)
	target_include_directories(sgre_platform_save PUBLIC include)
	target_compile_features(sgre_platform_save PUBLIC cxx_std_20)
	target_link_libraries(sgre_platform_save PUBLIC sgre_build_options)
	set_target_properties(sgre_platform_save PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
	if(SGRE_BUILD_TESTS)
		add_executable(backup_crypto_tests tests/backup_crypto_tests.cpp)
		target_link_libraries(backup_crypto_tests PRIVATE sgre_platform_save)
		set_target_properties(backup_crypto_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_backup_crypto COMMAND backup_crypto_tests)
	endif()
endif()

if(SGRE_BUILD_MACOS_MEDIA OR SGRE_BUILD_MACOS_RENDERER)
	# Audio delivery and rendering use one implementation of the actual continuous host clock.
	add_library(sgre_platform_clock STATIC src/platform/macos/host_performance_counter.cpp)
	target_include_directories(sgre_platform_clock PUBLIC include)
	target_compile_features(sgre_platform_clock PUBLIC cxx_std_20)
	target_link_libraries(sgre_platform_clock PUBLIC sgre_resource sgre_build_options)
	set_target_properties(sgre_platform_clock PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(SGRE_BUILD_MACOS_MEDIA)
	add_library(sgre_platform_media STATIC src/platform/macos/vp9_decoder.mm src/platform/macos/movie_rgb32.mm src/platform/macos/webm_playback.mm src/platform/macos/movie_opus_decoder.mm src/platform/macos/movie_silent_audio.mm)
	target_include_directories(sgre_platform_media PUBLIC include)
	target_compile_features(sgre_platform_media PUBLIC cxx_std_20)
	target_compile_options(sgre_platform_media PRIVATE -fobjc-arc)
	target_link_libraries(sgre_platform_media PUBLIC sgre_resource sgre_build_options "-framework CoreFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework VideoToolbox" "-framework Accelerate" "-framework Foundation" "-framework AVFAudio" "-framework AudioToolbox")
	set_target_properties(sgre_platform_media PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
endif()

if(SGRE_BUILD_MACOS_MEDIA)
	add_library(sgre_platform_audio STATIC src/platform/macos/offline_sound_mastering.mm src/platform/macos/offline_buffered_sound_pool.mm src/platform/macos/pcm_output_callback.cpp src/platform/macos/macos_pcm_output.mm)
	target_include_directories(sgre_platform_audio PUBLIC include)
	target_compile_features(sgre_platform_audio PUBLIC cxx_std_20)
	target_compile_options(sgre_platform_audio PRIVATE -fobjc-arc)
	target_link_libraries(sgre_platform_audio PUBLIC sgre_resource sgre_platform_clock sgre_build_options "-framework Foundation" "-framework AVFAudio" "-framework AudioToolbox" "-framework CoreAudio")
	set_target_properties(sgre_platform_audio PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
	target_link_libraries(sgre_platform_media PUBLIC sgre_platform_audio)
endif()

if(SGRE_BUILD_MACOS_RENDERER)
	add_library(sgre_platform_render STATIC src/platform/macos/color_pass.mm src/platform/macos/host_time.cpp src/platform/macos/application_pump.mm src/platform/macos/hid_controller_presence.mm src/platform/macos/sprite_graphics_targets.mm src/render/presentation_synchronization.cpp src/render/sprite_graphics_camera.cpp src/render/sprite_render_records.cpp src/render/frame_host_pool.cpp src/render/motion_sprite_record_receiver.cpp src/render/graphics_frame_cycle.cpp src/render/graphics_display_frame.cpp src/platform/macos/host_locale.mm src/platform/macos/metal_support.mm src/platform/macos/mutable_texture_backend.mm src/platform/macos/sprite_pass.mm src/platform/macos/sprite_presentation.mm src/platform/macos/motion_sprite_frame.mm)
	target_sources(sgre_platform_render PRIVATE src/platform/macos/emote_entropy.cpp src/render/screen_projection_record_receiver.cpp src/script/screen_projection_bindings.cpp src/script/native_texture_bindings.cpp src/platform/macos/motion_seed_source.cpp src/render/raw_texture_record_receiver.cpp src/render/captured_texture_record_receiver.cpp src/render/bitmap_font_record_receiver.cpp src/render/text_glyph_record_receiver.cpp src/render/movie_record_receiver.cpp src/render/picture_record_receiver.cpp)
	target_include_directories(sgre_platform_render PUBLIC include)
	if(SGRE_BUILD_TESTS)
		target_compile_definitions(sgre_platform_render PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
	endif()
	target_compile_features(sgre_platform_render PUBLIC cxx_std_20)
	target_compile_options(sgre_platform_render PRIVATE -fobjc-arc)
	target_link_libraries(sgre_platform_render PUBLIC sgre_resource sgre_platform_clock sgre_build_options "-framework Foundation" "-framework Metal" "-framework AppKit" "-framework QuartzCore" "-framework Security" "-framework CoreGraphics" "-framework IOKit")
	set_target_properties(sgre_platform_render PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
endif()

target_sources(sgre_resource PRIVATE
	src/scene/raw_layer_frame.cpp
	src/scene/motion_scene_evaluation.cpp
	src/scene/motion_shape_owner.cpp
	src/scene/motion_scene_player.cpp
	src/scene/motion_scene_draw_context.cpp
	src/scene/motion_scene_frame_commands.cpp
	src/scene/layer_base_frame.cpp
	src/script/native_regexp.cpp
	src/script/regexp_bindings.cpp
	src/save/backup_manager.cpp
	src/save/backup_files.cpp
	src/script/backup_bindings.cpp
	src/scene/bitmap_console.cpp
	src/text/bitmap_element_draw.cpp
	src/script/console_bindings.cpp
)

if(SGRE_BUILD_GAME)
	if(NOT APPLE OR NOT SGRE_BUILD_MACOS_MEDIA OR NOT SGRE_BUILD_MACOS_RENDERER)
		message(FATAL_ERROR "sgre-game currently requires the macOS media and renderer boundaries")
	endif()

	add_executable(sgre-game src/platform/macos/original_script_startup.mm src/platform/macos/content_digest.mm src/platform/macos/checkpoint_preparation.mm)
	target_link_libraries(sgre-game PRIVATE sgre_resource sgre_platform_render sgre_platform_save sgre_platform_media)
	target_compile_options(sgre-game PRIVATE -fobjc-arc -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
	set_target_properties(sgre-game PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/original_script_startup.mm")
	file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/original_script_startup.mm" SGRE_GAME_HOST_SOURCE_SHA256)
	target_compile_definitions(sgre-game PRIVATE SGRE_PRIVATE_STARTUP_SOURCE_SHA256="${SGRE_GAME_HOST_SOURCE_SHA256}")
endif()

add_executable(sgre-inspect tools/inspect_main.cpp)
target_link_libraries(sgre-inspect PRIVATE sgre_resource)
set_target_properties(sgre-inspect PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(sgre-inspect PRIVATE -Werror)

if(APPLE)
	add_library(sgre_platform_inspect STATIC src/platform/macos/inspect_gui.mm)
	target_include_directories(sgre_platform_inspect PUBLIC include)
	target_compile_features(sgre_platform_inspect PUBLIC cxx_std_20)
	target_compile_options(sgre_platform_inspect PRIVATE -fobjc-arc -Werror)
	target_link_libraries(sgre_platform_inspect PUBLIC sgre_resource sgre_build_options "-framework Foundation" "-framework AppKit")
	set_target_properties(sgre_platform_inspect PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
	target_link_libraries(sgre-inspect PRIVATE sgre_platform_inspect)
	target_compile_definitions(sgre-inspect PRIVATE SGRE_HAS_INSPECT_GUI=1)
endif()

if(SGRE_BUILD_TESTS)
	enable_testing()
	set(SGRE_CORE_TEST_SUITES
		mzs_shell
		psb
		psb_jobs
		struct_storage
		struct_bindings
		application_finish_bindings
		sound_bindings
		queued_mounted_sound
		system_archive_read_mounts
		system_file_existence
		zstd
		file_reader
		archive_index
		archive_mounts
		squirrel_bytecode
		resource_cipher
		md5
		launch_configuration
		display_path
		original_debug_scene_entry
		original_checkpoint
		original_calendar_restore
		original_scene_seek
		scenario_search
		resource_cache
		resource_receiving_observation
		squirrel_vm
		squirrel_property_stack
		squirrel_instanceof
		squirrel_operand_stack
		squirrel_native_stack
		squirrel_native_caller
		squirrel_native_temporary
		squirrel_scratch_writer
		squirrel_class_temporary
		squirrel_class_type_tag
		squirrel_class_index
		squirrel_base_suspend
		squirrel_child_wakeup
		squirrel_native_capture
		squirrel_capture_receiver
		squirrel_table_lifetime
		squirrel_table_clear
		squirrel_userdata
		squirrel_native_allocated
		squirrel_native_counter
		squirrel_call_result
		squirrel_delegate_default
		squirrel_meta_weak
		squirrel_error_assignment
		squirrel_get_failure_phase
		squirrel_nested_get
		squirrel_read_context
		squirrel_borrowed_receiver
		squirrel_nested_receiver
		image
		font
		webm
		motion
		motion_mesh
		sound
		sound_configuration_loads
		sound_archive_resources
		wave_decoder
		opus
		motion_evaluator
		motion_attribute_policy
		motion_deformation
		motion_bezier_lattice
		motion_affine_decomposition
		motion_mesh_lattice_override
		motion_join
		motion_bezier_inverse
		motion_bezier_degree56
		motion_deformation_chain
		motion_geometry
		motion_tessellation
		motion_source_registry
		motion_draw
		motion_grid_draw
		sprite_pipeline
		script_scheduler
		script_endpoint_observation
		render_target_layout
		graphics_model_stack
		graphics_model_reset
		frame_statistics
		thread_bindings
		thread_exit
		frame_clock
		thread_task_frame
		looper_frame
		looper_containing_phase
		looper_containing_mutation
		parallel_target_extraction
		application_event_control
		window_frame_pacing
		presentation_rectangle
		application_clock_pump
		sound_auxiliary_pause
		sound_voice_control
		sound_play_voice
		sound_master_volume
		sound_group_volume
		sound_voice_query
		locale
		object_bindings
		sqrat_value_weakref
		emote_bindings
		binary_bindings
		psb_bindings
		resource_bindings
		native_format
		driver_logs
		native_math
		random_bindings
		packed_color_bindings
		wma_packet
		wma_stream
		wma_exponent
		wma_scaling
		wma_transform
		wma_window
		wma_block
		wma_coefficient
		layers
		layer_manager_traversal
		layer_manager_frame
		layer_folder_frame
		base_layer_owner
		backup_owner
		backup_file_writer
		console_bindings
		bitmap_element_draw
		raw_layer_frame
		regexp
		default_driver_scene
		layer_frame_owner
		layer_frame_state_owner
		motion_layer_frame
		motion_frame_lifetime
		affine_layer_parameters
		affine_layer_order
		affine_layer_order_workspace
		affine_atan2
		affine_matrix
		affine_matrix_geometry
		affine_plane_transform
		picture_regions
		picture_layer_task
		picture_packet
		vertex_modulator
		affine_trigonometry
		affine_matrix_bindings
		affine_layer_matrices
		affine_layer_links
		affine_layer_bindings
		affine_priority_attachment
		layer_scale_view
		layer_camera_draw
		live_layer_camera_model
		live_layer_camera_state
		indicator_placement
		bitmap_indicator
		bitmap_font_assets
		bitmap_font_receiving
		bitmap_font_preparation
		bitmap_font_resource_publication
		motion_resource_publication
		parallel_task_set
		serial_task_set
		common_process_tasks
		indicator_bindings
		font_bindings
		raw_image_bindings
		raw_texture_bindings
		layer_raw_texture_bindings
		draw_object_bindings
		layer_draw_bindings
		image_assets
		image_coordinate_scalars
		image_receiving_document
		image_resource_publication
		image_bindings
		layer_picture_bindings
		layer_proxy_bindings
		layer_proxy_target_family
		fill_rectangle_bindings
		capture_bindings
		captured_bindings
		captured_opacity_animation
		captured_opacity_map
		object_finalizer_context
		mutable_texture_gpu_write
		vector_font_bindings
		layer_bindings
		layer_native_owner
		layer_view_bindings
		motion_layer_bindings
		text_encoding
		text_indicator_text
		text_layout
		text_line_geometry
		wma_decoder
		text_wrapping
		wma_stream_decoder
		wma_stream_source
		buffered_sound_streaming
		text_markup
		text_reveal
		text_bitmap_draw
		sound_playback
		buffered_sound_source_data
		buffered_sound_sources
		buffered_sound_cues
		voice_schedule
		text_element_draw
		input_device
		input_key_mapping
		pcm_output_ring
		pcm_output_delivery
		pcm_output_submission
		application_finish_event
		system_termination
		input_bindings
		game_bootstrap_input
		input_frame_owner
		keyboard_text
		text_glyph_run
		input_mouse
		voice_allocation
		sfnt
		truetype_outline
		truetype_glyph_path
		truetype_program
		truetype_stack_program
		truetype_control_program
		truetype_state_program
		truetype_point_program
		truetype_vector_font_metric
		truetype_hinted_glyph
		text_truetype_hinted_integration
		voice_uniqueness
		squirrel_lifetime
		sound_volume
		sound_volume_animation
		text_block_geometry
		text_render_plan
		text_vector_metrics
		text_vector_font
		text_vector_glyph_cache
		text_vector_glyph_draw
		text_font_selection
		text_shaping_features
		text_shaping_feature_map
		text_shaping_font_map
		text_shaping_lookup_plan
		text_single_substitution
		text_unicode_properties
		text_unicode_preprocess
		text_initial_clusters
		text_direction_adjustment
		text_combining_reorder
		text_nominal_glyph
		text_variation_glyph
		text_unicode_mirroring
		text_unicode_decomposition
		text_recursive_decomposition
		text_neutral_normalization
		text_neutral_recomposition
		text_unicode_composition
		text_formatting
		text_font_request
		text_font_measurement
		text_vector_unit_measurement
		text_vector_unit_extension_pair
		text_content_owner
		text_markup_font_reset
		text_ruby_content
		text_vector_run
		text_face_names
		text_property_settings
		text_font_info
		text_font_owner
		text_render_font_owner
		text_render_context
		text_render_option
		text_font_clear_binding
		text_glyph_drawing
		text_render_cursor
		text_characters
		text_render_bindings
		opentype_layout
		opentype_positioning
		opentype_glyph_properties
		motion_assets
		motion_receiving_document
		motion_batch_flush
		motion_clip_bounds
		motion_projects
		motion_timeline_selection
		motion_timeline_metadata
		motion_player_commands
		motion_player_update
		motion_root_scene
		motion_layer_composition
		motion_mesh_numeric_composition
		motion_selected_mesh_geometry
		motion_selected_mesh_ancestry
		motion_selected_mesh_vertices
		motion_selected_mesh_mask_packets
		motion_nonempty_inherited_mask
		motion_local_frames
		motion_layer_mesh_local_owner
		motion_stencil_final_slot_cursor
		motion_selected_local_numeric
		motion_tween_frames
		motion_cached_frames
		motion_hierarchy_kind2
		motion_hierarchy_composition
		motion_static_source
		motion_blank_source
		motion_blank_deformation
		motion_icon_mesh_grid_retention
		motion_parameter_range
		motion_parameter_values
		motion_priority_cursor
		motion_quad_batch
		motion_grid_batch
		motion_render_queue
		motion_mask_plan
		motion_frame_submission
		motion_masked_blend
		motion_stencil_state
		motion_shader_program
		motion_copy_target
		motion_copied_target_owner
		motion_copied_region_owner
		motion_texture_binding
		motion_copy_region
		motion_alternate_program
		raw_texture_color
		raw_image
		mutable_texture_owner
		raw_texture_packet
		raw_texture_restore
		raw_texture_task
		raw_layer_owner
		raw_layer_binding
		motion_mask_shader
		motion_scissor
		motion_resource_owner
		motion_resources
		motion_textures
		motion_shared_textures
		motion_alternate_texture_source
		motion_shape_owner
		motion_layer_getter_bindings
		squirrel_close
		cff
		cff_driver_hints
		cff_outline
		cff_geometry
		text_cff_glyph_metrics
		text_cff_hinted_metrics
		text_cff_glyph_font
		text_cff_glyph_atlas
		text_cff_glyph_path
		cff_glyph_stroke
		cff_glyph_stroke_curve
		cff_glyph_stroke_path
		text_cff_stroke_wrapper
		text_cff_hint_map
		text_cff_hint_geometry
		text_cff_contour_junction
		text_cff_pending_contour
		text_cff_contour_owner
		text_cff_contour_outline
		text_cff_default_hinting
		text_cff_hinted_charstring
		text_cff_stem_map
		text_cff_stem_declarations
		text_cff_charstring_mask
		text_glyph_atlas
		text_glyph_atlas_owner
		text_font_info_atlas
		text_glyph_raster_edges
		text_glyph_raster_cells
		text_glyph_raster_solid_copy
		text_cff_blue_zones
		texture_cache
		squirrel_string
		template_strings
		source_compiler
		emote_selector_controls
		emote_selector_scheduler
		emote_easing
		emote_skip_transition
		emote_variable_controls
		emote_timeline_tracks
		emote_soft_pendulum
		emote_catenary_soft
		emote_catenary_physics
		emote_catenary_control
		emote_bobble_model
		emote_bobble_sections
		emote_jelly_profile
		emote_jelly_sample_fit
		emote_jelly_section_rings
		emote_jelly_surface_samples
		emote_jelly_surface_bindings
		emote_jelly_physics
		emote_jelly_definition
		emote_jelly_control
		emote_tress_model
		emote_tress_spline
		emote_string_physics
		emote_string_mesh_output
		emote_string_control
		emote_synchronize_control
		emote_stretch_control
		emote_synchronize_native
		emote_collider
		emote_bobble_ellipse_collision
		emote_physical_math
		emote_jelly_collider_response
		emote_emission_pool
		emote_emission_emitter
		emote_emission_owner
		emote_scene_player
		emote_physical_assignment
		emote_player_bindings
		emote_wind
		emote_timeline_scheduler
		emote_timeline_additive
		emote_bloom_physics
		emote_bloom_output
		emote_bloom_correction
		emote_bloom_control
		emote_collision_owner
		emote_rigid_region
		emote_mesh_fix_alignment
		emote_string_uid_input
		emote_string_uid_origin
		emote_velvety_posture
		emote_velvety_frame
		emote_point_prediction
		emote_graph_control
		emote_blink_control
		emote_clamp_control
		screen_projection_packet
		squirrel_mark_order
		squirrel_host_root_order
		squirrel_collection_mutation
		squirrel_root_phase
		squirrel_set_root_fallback
		squirrel_member_lifetime
		squirrel_closure_lifetime
		squirrel_native_closure_lifetime
		squirrel_context_root
		squirrel_context_collectable
		squirrel_tail_varargs
		squirrel_error_state
		squirrel_native_depth
		background_calls
		file_read_serial
		driver_phases
		squirrel_call_pop
		motion_child_phase
		text_neutral_glyph_preparation
		text_initial_dotted_circle
		resource_load_frontier
		raw_read_destination
		motion_child_selection
		motion_child_root
		motion_child_direction
		text_glyph_definition_blob
		motion_child_target
		motion_direction_math
		text_glyph_substitution_blob
		text_glyph_positioning_blob
		text_layout_lookup_accelerator
		persistent_raw_queue
		motion_child_direction_phase
		motion_child_initial_composition
		motion_layer_clip_rectangles
		motion_child_clip_inheritance
		wrapped_psb_destination
		text_blob_font_face
		motion_random
		motion_seed_buffer
		performance_counter
		motion_child_configuration
		motion_child_construction
		text_lazy_layout_cache
		file_read_processor
		motion_child_frame_slots
		motion_child_frame_selection
		motion_layer_identifier
		motion_selected_layer_nodes
		motion_selected_layer_sources
		motion_stencil_mask_owner
		motion_particle_definition
		motion_particle_frame_slots
		motion_particle_emission
		motion_particle_player_state
		motion_particle_birth
		motion_particle_retirement
		motion_particle_scene
		motion_particle_clone
		motion_particle_rows
		movie_packet
		movie_layer_bindings
		movie_audio_throttle
		motion_player_velocity
		motion_layer_stencil_ancestry
		motion_stencil_row_policies
		motion_recursive_stencil_rows
		motion_child_join_eligibility
		motion_stencil_boolean
		text_vector_font_normalization_font
		text_vector_font_shape_plan
		text_shape_plan_owner_diagnostic
		function_thread_shutdown
		resource_file_service
		resource_file_service_vm
		text_unicode_buffer_input
		text_unicode_buffer_position
		text_unicode_buffer_font
		motion_initial_traversal
		script_read_buffer
		script_buffer_load
		script_thread_file
		queued_script_thread_file
		motion_selected_sprite_player
		resource_manager_process
		resource_manager_thread_file
		text_cff_unhinted_font
		text_vector_font_metric_owner
		motion_player_task
		motion_scene_player_task motion_kind7_scene
		motion_scene_clone
		text_vector_font_default_metric
		resource_meter_process
		resource_meter_constructor_admission
		prepared_driver_close
		prepared_driver_leaf_preflight
		motion_native_owners
		motion_bindings
		motion_runtime_lookup
	)

	add_test(NAME system_termination_exit_child COMMAND ${CMAKE_COMMAND} "-DTEST_EXECUTABLE=$<TARGET_FILE:system_termination_tests>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/system_termination_child.cmake")
	set_tests_properties(system_termination_exit_child PROPERTIES TIMEOUT 15 ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:abort_on_error=1;UBSAN_OPTIONS=halt_on_error=1")
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/startup-dependency-observer-stage/override.sqbc")
		add_test(NAME application_finish_original_callback COMMAND application_finish_event_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/startup-dependency-observer-stage/override.sqbc")
	endif()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/emote-wind-owner-stage/native-wind-expected.bin")
		add_test(NAME emote_wind_native COMMAND emote_wind_tests --native-wind "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/emote-wind-owner-stage/native-wind-expected.bin")
	endif()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/emote-catenary-entity-stage/native-vectors.bin")
		add_test(NAME emote_catenary_physics_native COMMAND emote_catenary_physics_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/emote-catenary-entity-stage/native-vectors.bin")
	endif()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/emote-catenary-soft-stage/native-vectors.bin")
		add_test(NAME emote_catenary_soft_native COMMAND emote_catenary_soft_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/emote-catenary-soft-stage/native-vectors.bin")
	endif()

	add_executable(motion_bezier_surface12_tests tests/motion_bezier_surface12_tests.cpp)
	target_link_libraries(motion_bezier_surface12_tests PRIVATE sgre_resource)
	set_target_properties(motion_bezier_surface12_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-bezier-degree12-stage/native-vectors.bin")
		add_test(NAME motion_bezier_surface12 COMMAND motion_bezier_surface12_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-bezier-degree12-stage/native-vectors.bin")
	endif()

	add_executable(motion_bezier_inverse12_tests tests/motion_bezier_inverse12_tests.cpp)
	target_link_libraries(motion_bezier_inverse12_tests PRIVATE sgre_resource)
	set_target_properties(motion_bezier_inverse12_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-bezier-degree12-stage/inverse/native-vectors.bin")
		add_test(NAME motion_bezier_inverse12 COMMAND motion_bezier_inverse12_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-bezier-degree12-stage/inverse/native-vectors.bin")
	endif()

	add_executable(motion_bezier_surface56_tests tests/motion_bezier_surface56_tests.cpp)
	target_link_libraries(motion_bezier_surface56_tests PRIVATE sgre_resource)
	set_target_properties(motion_bezier_surface56_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-bezier-surface56-stage/native-vectors.bin")
		add_test(NAME motion_bezier_surface56 COMMAND motion_bezier_surface56_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-bezier-surface56-stage/native-vectors.bin")
	endif()

	add_executable(motion_lattice_degree_conversion_tests tests/motion_lattice_degree_conversion_tests.cpp)
	target_link_libraries(motion_lattice_degree_conversion_tests PRIVATE sgre_resource)
	set_target_properties(motion_lattice_degree_conversion_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-lattice-degree-conversion-stage/native-vectors.bin")
		add_test(NAME motion_lattice_degree_conversion COMMAND motion_lattice_degree_conversion_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-lattice-degree-conversion-stage/native-vectors.bin")
	endif()

	# Compile the exact host owner directly for its private uint64 wrap probe.
	add_executable(frame_host_heap_tests src/render/frame_host_heap.cpp tests/frame_host_heap_tests.cpp)
	target_include_directories(frame_host_heap_tests PRIVATE include)
	target_compile_features(frame_host_heap_tests PRIVATE cxx_std_20)
	target_compile_definitions(frame_host_heap_tests PRIVATE SGRE_FRAME_HOST_HEAP_FIXTURE=1)
	target_link_libraries(frame_host_heap_tests PRIVATE sgre_build_options Threads::Threads)
	set_target_properties(frame_host_heap_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/per-frame-host-heap-stage/native-host-heap.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/per-frame-host-heap-stage/native-zero-before-begin.bin")
		add_test(NAME frame_host_heap COMMAND frame_host_heap_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/per-frame-host-heap-stage/native-host-heap.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/per-frame-host-heap-stage/native-zero-before-begin.bin")
	endif()
	if(SGRE_BUILD_MACOS_MEDIA)
		add_executable(offline_buffered_sound_rate_tests tests/offline_buffered_sound_rate_tests.cpp)
		target_link_libraries(offline_buffered_sound_rate_tests PRIVATE sgre_resource sgre_platform_audio)
		set_target_properties(offline_buffered_sound_rate_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_offline_buffered_sound_rate COMMAND offline_buffered_sound_rate_tests "${CMAKE_CURRENT_BINARY_DIR}/offline-buffered-sound-rate-fixtures")
		set_tests_properties(macos_offline_buffered_sound_rate PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(offline_buffered_sound_pool_tests tests/offline_buffered_sound_pool_tests.cpp)
		target_link_libraries(offline_buffered_sound_pool_tests PRIVATE sgre_resource sgre_platform_audio)
		set_target_properties(offline_buffered_sound_pool_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_offline_buffered_sound_pool COMMAND offline_buffered_sound_pool_tests "${CMAKE_CURRENT_BINARY_DIR}/offline-buffered-sound-pool-fixtures")
		set_tests_properties(macos_offline_buffered_sound_pool PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 60 RUN_SERIAL TRUE)

		foreach(test_name IN ITEMS offline_wma_stream streaming_sound_cues streaming_sound_close sound_streaming_policy)
			add_executable(${test_name}_tests tests/${test_name}_tests.cpp)
			target_link_libraries(${test_name}_tests PRIVATE sgre_resource sgre_platform_audio)
			set_target_properties(${test_name}_tests PROPERTIES CXX_EXTENSIONS OFF)
			add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-fixtures")
			set_tests_properties(macos_${test_name} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 60 RUN_SERIAL TRUE)
		endforeach()

		add_executable(pcm_output_callback_tests src/platform/macos/tests/pcm_output_callback_tests.cpp)
		target_include_directories(pcm_output_callback_tests PRIVATE src/platform/macos)
		target_link_libraries(pcm_output_callback_tests PRIVATE sgre_resource sgre_platform_audio)
		set_target_properties(pcm_output_callback_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_pcm_output_callback COMMAND pcm_output_callback_tests)

		foreach(test_name IN ITEMS pcm_output_delivery_callback pcm_output_submission_callback)
			add_executable(${test_name}_tests src/platform/macos/tests/${test_name}_tests.cpp)
			target_include_directories(${test_name}_tests PRIVATE src/platform/macos)
			target_link_libraries(${test_name}_tests PRIVATE sgre_resource sgre_platform_audio)
			set_target_properties(${test_name}_tests PROPERTIES CXX_EXTENSIONS OFF)
			add_test(NAME macos_${test_name} COMMAND ${test_name}_tests)
			set_tests_properties(macos_${test_name} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
		endforeach()

		add_executable(offline_sound_mastering_tests tests/offline_sound_mastering_tests.cpp tests/offline_sound_boundary_tests.mm)
		target_compile_options(offline_sound_mastering_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
		target_link_libraries(offline_sound_mastering_tests PRIVATE sgre_resource sgre_platform_audio)
		set_target_properties(offline_sound_mastering_tests PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_offline_sound_mastering COMMAND offline_sound_mastering_tests "${CMAKE_CURRENT_BINARY_DIR}/offline-sound-mastering-fixtures")
		set_tests_properties(macos_offline_sound_mastering PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(offline_sound_boundary_tests tests/offline_sound_boundary_tests.mm)
		target_include_directories(offline_sound_boundary_tests PRIVATE src/platform/macos)
		target_compile_options(offline_sound_boundary_tests PRIVATE -fobjc-arc)
		target_compile_definitions(offline_sound_boundary_tests PRIVATE SGRE_OFFLINE_SOUND_BOUNDARY_STANDALONE=1)
		target_link_libraries(offline_sound_boundary_tests PRIVATE sgre_build_options "-framework Foundation")
		set_target_properties(offline_sound_boundary_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_offline_sound_boundary COMMAND offline_sound_boundary_tests)
	endif()

	foreach(test_name IN LISTS SGRE_CORE_TEST_SUITES)
		add_executable(${test_name}_tests tests/${test_name}_tests.cpp)
		target_link_libraries(${test_name}_tests PRIVATE sgre_resource)
		set_target_properties(${test_name}_tests PROPERTIES CXX_EXTENSIONS OFF)
		if((test_name STREQUAL "backup_owner" OR test_name STREQUAL "backup_file_writer") AND APPLE)
			target_link_libraries(${test_name}_tests PRIVATE sgre_platform_save)
		endif()
		if(test_name STREQUAL "text_lazy_layout_cache" AND APPLE AND BUILD_SHARED_LIBS)
			# Dylib allocations bypass the executable fault hook; use the exact public units in this focused fixture.
			target_sources(${test_name}_tests PRIVATE src/text/blob_font_face.cpp src/text/blob_font_layout_cache.cpp src/text/glyph_definition_blob.cpp src/text/glyph_substitution_blob.cpp src/text/glyph_positioning_blob.cpp src/text/layout_lookup_accelerator.cpp)
			target_compile_options(${test_name}_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -ffp-contract=off)
		endif()
		if(test_name STREQUAL "text_unicode_buffer_input" AND APPLE AND BUILD_SHARED_LIBS)
			# Preserve the dedicated paired-allocation fault boundary with exact public source units.
			target_sources(${test_name}_tests PRIVATE src/text/recursive_decomposition.cpp src/text/unicode_buffer_input.cpp src/text/nominal_glyph.cpp src/text/variation_glyph.cpp src/text/unicode_properties.cpp src/text/unicode_preprocess.cpp src/text/unicode_decomposition.cpp src/text/unicode_composition.cpp src/text/initial_clusters.cpp src/text/direction_adjustment.cpp src/resource/sfnt.cpp)
			target_compile_options(${test_name}_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -ffp-contract=off)
		endif()
		if(test_name STREQUAL "text_unicode_buffer_position" AND APPLE AND BUILD_SHARED_LIBS)
			# Keep the actual paired-allocation fault probe in this executable, including position storage.
			target_sources(${test_name}_tests PRIVATE src/text/recursive_decomposition.cpp src/text/unicode_buffer_input.cpp src/text/nominal_glyph.cpp src/text/variation_glyph.cpp src/text/unicode_properties.cpp src/text/unicode_preprocess.cpp src/text/unicode_decomposition.cpp src/text/unicode_composition.cpp src/text/initial_clusters.cpp src/text/direction_adjustment.cpp src/resource/sfnt.cpp src/text/unicode_buffer_positions.cpp)
			target_compile_options(${test_name}_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -ffp-contract=off)
		endif()
		if(test_name STREQUAL "raw_read_destination")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/raw-read-fixtures")
		elseif(test_name STREQUAL "wrapped_psb_destination")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/wrapped-psb-fixtures")
		elseif(test_name STREQUAL "system_archive_read_mounts")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/system-read-mount-fixtures")
		elseif(test_name STREQUAL "file_read_processor")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/file-read-processor-fixtures")
		elseif(test_name STREQUAL "bitmap_font_resource_publication")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/bitmap-font-publication-fixtures")
		elseif(test_name STREQUAL "image_resource_publication")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/image-publication-fixtures")
		elseif(test_name STREQUAL "motion_resource_publication")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/motion-publication-fixtures")
		elseif(test_name STREQUAL "resource_file_service")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/resource-file-service-fixtures")
		elseif(test_name STREQUAL "queued_mounted_sound")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/queued-mounted-sound-fixtures")
		elseif(test_name STREQUAL "thread_task_frame")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/application-thread-frame-stage/native-worker-cost.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/application-thread-frame-stage/native-worker-cost.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "looper_frame")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/looper-frame-phase-stage/native-looper-frame-expanded.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/looper-frame-phase-stage/native-looper-frame-expanded.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "window_frame_pacing")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/window-frame-pacing-stage/window-pacing-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/window-frame-pacing-stage/window-pacing-native.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "render_target_layout")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/render-target-layout-stage/render-target-layout-native.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/render-target-clear-color-stage/render-target-clear-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/render-target-layout-stage/render-target-layout-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/render-target-clear-color-stage/render-target-clear-native.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "script_endpoint_observation")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/script-endpoint-fixtures")
		elseif(test_name STREQUAL "application_event_control")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/application-event-control-stage/event-control-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/application-event-control-stage/event-control-native.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "motion_layer_mesh_local_owner")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mesh-local-owner-stage/mesh-local-owner-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mesh-local-owner-stage/mesh-local-owner-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_stencil_final_slot_cursor")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/stencil-final-slot-cursor-correction-stage/stencil-final-slots-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/stencil-final-slot-cursor-correction-stage/stencil-final-slots-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_mesh_numeric_composition")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mesh-numeric-composition-stage/mesh-numeric-composition-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mesh-numeric-composition-stage/mesh-numeric-composition-native.bin")
			endif()
		elseif(test_name STREQUAL "application_clock_pump")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/application-clock-pump-stage/application-clock-pump-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/application-clock-pump-stage/application-clock-pump-native.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "graphics_model_stack")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-model-stack-stage/graphics-model-stack-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-model-stack-stage/graphics-model-stack-native.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "motion_selected_layer_sources")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-registered-source-owner-stage/registered-source-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-registered-source-owner-stage/registered-source-native.bin")
			endif()
		elseif(test_name STREQUAL "resource_receiving_observation")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/resource-receiving-observation-fixtures")
		elseif(test_name STREQUAL "motion_selected_mesh_geometry")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mesh-geometry-owner-stage/selected-mesh-geometry-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mesh-geometry-owner-stage/selected-mesh-geometry-native.bin")
			endif()
		elseif(test_name STREQUAL "sound_auxiliary_pause")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-auxiliary-pause-stage/native-auxiliary-control.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-auxiliary-pause-fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-auxiliary-pause-stage/native-auxiliary-control.bin")
			endif()
		elseif(test_name STREQUAL "motion_grid_batch")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-frame-grid-batch-native-fill-stage/native-grid-batch.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/motion-frame-grid-batch-native-fill-stage/native-grid-batch.bin")
			else()
				add_test(NAME ${test_name} COMMAND ${test_name}_tests)
			endif()
		elseif(test_name STREQUAL "motion_selected_mesh_ancestry")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mixed-mesh-ancestry-stage/mixed-mesh-ancestry-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-mixed-mesh-ancestry-stage/mixed-mesh-ancestry-native.bin")
			endif()
		elseif(test_name STREQUAL "sound_play_voice")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-play-voice-fixtures")
		elseif(test_name STREQUAL "launch_configuration")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}")
		elseif(test_name STREQUAL "sound_volume_animation")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-volume-animation-owner-stage/native-volume-animation.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-volume-animation-fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-volume-animation-owner-stage/native-volume-animation.bin")
			endif()
		elseif(test_name STREQUAL "sound_group_volume")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-group-volume-fixtures")
		elseif(test_name STREQUAL "sound_master_volume")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-master-volume-fixtures")
		elseif(test_name STREQUAL "sound_voice_control")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-voice-control-owner-stage/voice-selector-result-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-voice-control-fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-voice-control-owner-stage/voice-selector-result-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_selected_mesh_vertices")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-inherited-mesh-vertices-stage/inherited-mesh-vertices-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-inherited-mesh-vertices-stage/inherited-mesh-vertices-native.bin")
			endif()
		elseif(test_name STREQUAL "sound_voice_query")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-voice-query-owner-stage/voice-query-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-voice-query-fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/sound-voice-query-owner-stage/voice-query-native.bin")
			endif()
		elseif(test_name STREQUAL "graphics_model_reset")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-model-reset-owner-stage/model-reset-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-model-reset-owner-stage/model-reset-native.bin")
			endif()
		elseif(test_name STREQUAL "live_layer_camera_model")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/layer_camera_draw/camera-draw-projection.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/layer_camera_draw/camera-draw-projection.bin")
			endif()
		elseif(test_name STREQUAL "frame_statistics")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/render-frame-statistics-stage/frame-statistics-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/render-frame-statistics-stage/frame-statistics-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_selected_mesh_mask_packets")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parent-clock-mask-correction-stage/corrected-empty-mask-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parent-clock-mask-correction-stage/corrected-empty-mask-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_nonempty_inherited_mask")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parent-clock-mask-correction-stage/corrected-nonempty-mask-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parent-clock-mask-correction-stage/corrected-nonempty-mask-native.bin")
			endif()
		elseif(test_name STREQUAL "live_layer_camera_state")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/layer_camera_draw/camera-draw-projection.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/layer_camera_draw/camera-draw-projection.bin")
			endif()
		elseif(test_name STREQUAL "motion_blank_source")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-blank-source-rectangle-stage/blank-rectangle-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-blank-source-rectangle-stage/blank-rectangle-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_blank_deformation")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-blank-deformation-consumer-stage/blank-deformation-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-blank-deformation-consumer-stage/blank-deformation-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_icon_mesh_grid_retention")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-icon-mesh-grid-retention-stage/icon-mesh-grid-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-icon-mesh-grid-retention-stage/icon-mesh-grid-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_parameter_values")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-variable-owner-stage/parent-fanout-native.bin")
				add_test(NAME motion_parameter_values COMMAND motion_parameter_values_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-variable-owner-stage/parent-fanout-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_parameter_range")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-initial-state-stage/parameter-initial-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-initial-state-stage/parameter-initial-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_selected_local_numeric")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/child-join-eligibility-stage/child-join-eligibility-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/child-join-eligibility-stage/child-join-eligibility-native.bin")
			endif()
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-local-numeric-stage/selected-local-numeric-native.bin")
				add_test(NAME motion_selected_local_numeric_controls COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-local-numeric-stage/selected-local-numeric-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_child_join_eligibility")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/child-join-eligibility-stage/child-join-eligibility-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/child-join-eligibility-stage/child-join-eligibility-native.bin")
			endif()
		elseif(test_name STREQUAL "motion_recursive_stencil_rows")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/recursive-stencil-row-stage/recursive-stencil-row-native.bin")
				add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/recursive-stencil-row-stage/recursive-stencil-row-native.bin")
			endif()
		elseif(test_name STREQUAL "sound_bindings")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-bindings-fixtures")
		elseif(test_name STREQUAL "sound_configuration_loads")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-configuration-loads-fixtures")
		elseif(test_name STREQUAL "buffered_sound_cues")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/buffered-sound-cues-fixtures")
		elseif(test_name STREQUAL "sound_archive_resources")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/sound-archive-resources-fixtures")
		elseif(test_name STREQUAL "resource_file_service_vm")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/resource-file-service-vm-fixtures")
		elseif(test_name STREQUAL "script_read_buffer")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/script-read-buffer-fixtures")
		elseif(test_name STREQUAL "script_buffer_load")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/script-buffer-load-fixtures")
		elseif(test_name STREQUAL "queued_script_thread_file")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/queued-script-thread-fixtures")
		elseif(test_name STREQUAL "script_thread_file")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/script-thread-file-fixtures")
		elseif(test_name STREQUAL "resource_manager_process")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/resource-manager-process-fixtures")
		elseif(test_name STREQUAL "resource_manager_thread_file")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/resource-manager-thread-file-fixtures")
		elseif(test_name STREQUAL "motion_native_owners")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/motion-native-owner-fixtures")
		elseif(test_name STREQUAL "motion_bindings")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/motion-binding-fixtures")
		elseif(test_name STREQUAL "prepared_driver_close")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/prepared-driver-close-fixtures")
		elseif(test_name STREQUAL "prepared_driver_leaf_preflight")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/prepared-driver-leaf-preflight-fixtures")
		elseif(test_name STREQUAL "persistent_raw_queue")
			add_test(NAME ${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_BINARY_DIR}/persistent-raw-fixtures")
		else()
			add_test(NAME ${test_name} COMMAND ${test_name}_tests)
		endif()
	endforeach()

	# Original bytecode is a local optional fixture; no script contents are distributed.
	set(SGRE_SEEK_ENVPLAYER "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/original-direct-gameplay-entry-stage/envplayer.nut")
	set(SGRE_SEEK_UTIL "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/original-debug-scene-entry-util-stage/util.nut")
	set(SGRE_SEEK_INIT "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/original-stateful-scene-seek-owner-stage/inputs/init.nut")
	if(EXISTS "${SGRE_SEEK_ENVPLAYER}" AND EXISTS "${SGRE_SEEK_UTIL}" AND EXISTS "${SGRE_SEEK_INIT}")
		add_test(NAME original_scene_seek_original COMMAND original_scene_seek_tests "${SGRE_SEEK_ENVPLAYER}" "${SGRE_SEEK_UTIL}" "${SGRE_SEEK_INIT}")
		set_tests_properties(original_scene_seek_original PROPERTIES TIMEOUT 15)
	endif()

	if(APPLE)
		add_executable(scene_checkpoint_cache_tests tests/scene_checkpoint_cache_tests.cpp src/platform/macos/content_digest.mm)
		target_link_libraries(scene_checkpoint_cache_tests PRIVATE sgre_resource)
		target_compile_options(scene_checkpoint_cache_tests PRIVATE -Werror)
		set_target_properties(scene_checkpoint_cache_tests PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME scene_checkpoint_cache COMMAND scene_checkpoint_cache_tests)
		add_executable(checkpoint_preparation_tests tests/checkpoint_preparation_tests.cpp src/platform/macos/checkpoint_preparation.mm src/platform/macos/content_digest.mm)
		target_link_libraries(checkpoint_preparation_tests PRIVATE sgre_resource)
		target_compile_options(checkpoint_preparation_tests PRIVATE -Werror)
		set_target_properties(checkpoint_preparation_tests PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME checkpoint_preparation COMMAND checkpoint_preparation_tests)
	endif()

	add_executable(original_scene_seek_movie_tests tests/original_scene_seek_movie_tests.cpp)
	target_link_libraries(original_scene_seek_movie_tests PRIVATE sgre_resource)
	set_target_properties(original_scene_seek_movie_tests PROPERTIES CXX_EXTENSIONS OFF)
	set(SGRE_SEEK_MOVIE_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/original-stateful-scene-seek-movie-control-stage/inputs")
	set(SGRE_SEEK_EXCEPTION "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/original-movie-button-exception-read-display-stage/exception.nut")
	if(EXISTS "${SGRE_SEEK_UTIL}" AND EXISTS "${SGRE_SEEK_MOVIE_INPUTS}/override.nut" AND EXISTS "${SGRE_SEEK_MOVIE_INPUTS}/exdialog.nut" AND EXISTS "${SGRE_SEEK_MOVIE_INPUTS}/motionpanel.nut" AND EXISTS "${SGRE_SEEK_MOVIE_INPUTS}/confirmdialog.nut" AND EXISTS "${SGRE_SEEK_EXCEPTION}")
		add_test(NAME original_scene_seek_movie COMMAND original_scene_seek_movie_tests "${SGRE_SEEK_UTIL}" "${SGRE_SEEK_MOVIE_INPUTS}/override.nut" "${SGRE_SEEK_MOVIE_INPUTS}/exdialog.nut" "${SGRE_SEEK_MOVIE_INPUTS}/motionpanel.nut" "${SGRE_SEEK_MOVIE_INPUTS}/confirmdialog.nut" "${SGRE_SEEK_EXCEPTION}")
		set_tests_properties(original_scene_seek_movie PROPERTIES TIMEOUT 15)
	endif()

	# Native comparison artifacts remain private and optional.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-native-stage/native-curve-vectors.bin")
		add_test(NAME cff_glyph_stroke_curve_native COMMAND cff_glyph_stroke_curve_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-native-stage/native-curve-vectors.bin")
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-actual-curve-stage/native-actual-curves.bin")
		add_test(NAME cff_glyph_stroke_curve_actual COMMAND cff_glyph_stroke_curve_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-actual-curve-stage/native-actual-curves.bin")
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/cff-glyph-stroke-path-stage/native-path-vectors.bin")
		add_test(NAME cff_glyph_stroke_path_native COMMAND cff_glyph_stroke_path_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/cff-glyph-stroke-path-stage/native-path-vectors.bin")
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/cff-glyph-stroke-degenerate-revision-stage/native-degenerate-vectors.bin")
		add_test(NAME cff_glyph_stroke_path_degenerate COMMAND cff_glyph_stroke_path_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/cff-glyph-stroke-degenerate-revision-stage/native-degenerate-vectors.bin")
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-empty-expanded-native-stage/prepared-input.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-empty-expanded-native-stage/native-placement-pixels.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-empty-expanded-native-stage/cases/native-final-atlas.bin")
		add_test(NAME text_cff_stroke_empty_expanded_native COMMAND text_cff_stroke_pixels_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-empty-expanded-native-stage/prepared-input.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-empty-expanded-native-stage/native-placement-pixels.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-empty-expanded-native-stage/cases/native-final-atlas.bin")
	endif()
	add_executable(text_cff_stroke_pixels_tests tests/text_cff_stroke_pixels_tests.cpp)
	target_link_libraries(text_cff_stroke_pixels_tests PRIVATE sgre_resource)
	set_target_properties(text_cff_stroke_pixels_tests PROPERTIES CXX_EXTENSIONS OFF)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-pixels-native-stage/hiragino-control-input.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-pixel-replay-stage/hiragino-native-placement-pixels.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-pixels-native-stage/hiragino-control/native-final-atlas.bin")
		add_test(NAME text_cff_stroke_pixels_native COMMAND text_cff_stroke_pixels_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-pixels-native-stage/hiragino-control-input.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-pixel-replay-stage/hiragino-native-placement-pixels.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/text-stroke-pixels-native-stage/hiragino-control/native-final-atlas.bin")
	endif()

	add_test(NAME prepared_driver_close_verify_destructor COMMAND prepared_driver_close_tests "${CMAKE_CURRENT_BINARY_DIR}/prepared-driver-close-destructor-fixtures" --verify-destroy-unclosed)
	add_test(NAME prepared_driver_leaf_verify_runtime_destructor COMMAND prepared_driver_leaf_preflight_tests "${CMAKE_CURRENT_BINARY_DIR}/prepared-driver-leaf-runtime-destructor-fixtures" --verify-destroy-runtime-first)
	set_tests_properties(prepared_driver_close prepared_driver_leaf_preflight prepared_driver_close_verify_destructor prepared_driver_leaf_verify_runtime_destructor PROPERTIES TIMEOUT 30)
	add_test(NAME resource_meter_destroy_detached COMMAND resource_meter_process_tests --verify-destroy-with-detached-clock)
	add_test(NAME resource_meter_destroy_active_callback COMMAND resource_meter_process_tests --verify-destroy-active-driver-callback)
	set_tests_properties(resource_meter_process resource_meter_constructor_admission resource_meter_destroy_detached resource_meter_destroy_active_callback PROPERTIES TIMEOUT 30)
	add_test(NAME motion_frame_destroy_active COMMAND motion_frame_lifetime_tests --verify-in-callback)
	add_test(NAME motion_frame_destroy_lease COMMAND motion_frame_lifetime_tests --verify-with-lease)
	add_test(NAME background_self_task_destruction COMMAND background_calls_tests --self-task-destruction)
	add_test(NAME background_live_manager_destruction COMMAND background_calls_tests --live-manager-destruction)
	add_test(NAME file_read_rejected_task_destruction COMMAND file_read_serial_tests --rejected-task-destruction)
	add_test(NAME file_read_rejected_set_destruction COMMAND file_read_serial_tests --rejected-set-destruction)
	add_test(NAME raw_read_queued_destination_destruction COMMAND raw_read_destination_tests --queued-destination-destruction)
	add_test(NAME raw_read_selected_destination_destruction COMMAND raw_read_destination_tests --selected-destination-destruction)
	set_tests_properties(raw_read_destination raw_read_queued_destination_destruction raw_read_selected_destination_destruction PROPERTIES TIMEOUT 30)
	add_test(NAME file_read_fatal_index_destruction COMMAND file_read_processor_tests "${CMAKE_CURRENT_BINARY_DIR}/file-read-processor-fixtures" --fatal-index-destruction)
	add_test(NAME file_read_receiving_owner_destruction COMMAND file_read_processor_tests "${CMAKE_CURRENT_BINARY_DIR}/file-read-processor-fixtures" --receiving-owner-destruction)
	set_tests_properties(file_read_processor file_read_fatal_index_destruction file_read_receiving_owner_destruction PROPERTIES TIMEOUT 30)
	add_test(NAME persistent_raw_attached_manager_destruction COMMAND persistent_raw_queue_tests --attached-manager-destruction)
	add_test(NAME persistent_raw_throwing_provider_destruction COMMAND persistent_raw_queue_tests "${CMAKE_CURRENT_BINARY_DIR}/persistent-raw-fixtures" --throwing-provider-destruction)
	add_test(NAME persistent_raw_throwing_provider_close_race COMMAND persistent_raw_queue_tests "${CMAKE_CURRENT_BINARY_DIR}/persistent-raw-fixtures" --throwing-provider-close-race)
	set_tests_properties(persistent_raw_queue persistent_raw_attached_manager_destruction persistent_raw_throwing_provider_destruction persistent_raw_throwing_provider_close_race PROPERTIES TIMEOUT 30)
	set_tests_properties(background_calls file_read_serial background_self_task_destruction background_live_manager_destruction file_read_rejected_task_destruction file_read_rejected_set_destruction PROPERTIES TIMEOUT 30)

	add_test(NAME script_buffer_pending_destruction COMMAND script_read_buffer_tests --pending-buffer-destruction)
	set_tests_properties(script_read_buffer script_buffer_load script_buffer_pending_destruction PROPERTIES TIMEOUT 30)
	add_test(NAME script_thread_pending_destruction COMMAND script_thread_file_tests --pending-thread-destruction "${CMAKE_CURRENT_BINARY_DIR}/script-thread-death-fixtures")
	set_tests_properties(script_thread_file script_thread_pending_destruction PROPERTIES TIMEOUT 30)

	if(SGRE_BUILD_MACOS_MEDIA)
		add_executable(movie_pcm_delivery_tests src/platform/macos/tests/movie_pcm_delivery_tests.cpp)
		target_include_directories(movie_pcm_delivery_tests PRIVATE src/platform/macos)
		target_link_libraries(movie_pcm_delivery_tests PRIVATE sgre_platform_media)
		set_target_properties(movie_pcm_delivery_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_movie_pcm_delivery COMMAND movie_pcm_delivery_tests)
		set_tests_properties(macos_movie_pcm_delivery PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 30 RUN_SERIAL TRUE)

		add_executable(movie_gain_tests tests/movie_gain_tests.cpp)
		target_link_libraries(movie_gain_tests PRIVATE sgre_platform_media)
		set_target_properties(movie_gain_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_movie_gain COMMAND movie_gain_tests)
		add_executable(movie_audio_tests tests/movie_audio_tests.cpp)
		target_link_libraries(movie_audio_tests PRIVATE sgre_platform_media)
		set_target_properties(movie_audio_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_movie_audio COMMAND movie_audio_tests)
		add_executable(webm_playback_tests tests/webm_playback_tests.cpp)
		target_link_libraries(webm_playback_tests PRIVATE sgre_platform_media)
		set_target_properties(webm_playback_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_webm_playback COMMAND webm_playback_tests)
		add_executable(movie_rgb32_tests tests/movie_rgb32_tests.cpp)
		target_link_libraries(movie_rgb32_tests PRIVATE sgre_platform_media)
		set_target_properties(movie_rgb32_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_movie_rgb32 COMMAND movie_rgb32_tests)
		add_executable(video_tests tests/video_tests.cpp)
		target_link_libraries(video_tests PRIVATE sgre_platform_media)
		set_target_properties(video_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_video COMMAND video_tests)
	endif()

	if(SGRE_BUILD_MACOS_RENDERER)
		# Build all guarded presentation consumers directly; the production platform archive keeps fixture hooks disabled.
		add_executable(sprite_presentation_tests
			src/render/presentation_synchronization.cpp
			src/platform/macos/sprite_pass.mm
			src/platform/macos/sprite_presentation.mm
			src/platform/macos/tests/sprite_presentation_tests.mm
			src/platform/macos/metal_support.mm
			src/platform/macos/mutable_texture_backend.mm
			src/platform/macos/motion_sprite_frame.mm
		)
		target_include_directories(sprite_presentation_tests PRIVATE include src/platform/macos)
		target_compile_features(sprite_presentation_tests PRIVATE cxx_std_20)
		target_compile_definitions(sprite_presentation_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_compile_options(sprite_presentation_tests PRIVATE -fobjc-arc -Werror)
		target_link_libraries(sprite_presentation_tests PRIVATE sgre_resource "-framework Foundation" "-framework Metal" "-framework AppKit" "-framework QuartzCore" "-framework Security")
		set_target_properties(sprite_presentation_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_presentation_lifetime COMMAND sprite_presentation_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal" lifetime)
		add_test(NAME macos_sprite_presentation_slots COMMAND sprite_presentation_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal" slots)
		set_tests_properties(macos_sprite_presentation_lifetime macos_sprite_presentation_slots PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(presentation_failure_tests
			src/render/presentation_synchronization.cpp
			src/platform/macos/sprite_pass.mm
			src/platform/macos/sprite_presentation.mm
			src/platform/macos/tests/presentation_failure_tests.mm
			src/platform/macos/metal_support.mm
			src/platform/macos/mutable_texture_backend.mm
		)
		target_include_directories(presentation_failure_tests PRIVATE include src/platform/macos)
		target_compile_features(presentation_failure_tests PRIVATE cxx_std_20)
		target_compile_definitions(presentation_failure_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_compile_options(presentation_failure_tests PRIVATE -fobjc-arc -Werror)
		target_link_libraries(presentation_failure_tests PRIVATE sgre_resource "-framework Foundation" "-framework Metal" "-framework AppKit" "-framework QuartzCore" "-framework Security")
		set_target_properties(presentation_failure_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_presentation_failure_boundary COMMAND presentation_failure_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_presentation_failure_boundary PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(host_performance_counter_tests src/platform/macos/tests/host_performance_counter_tests.cpp)
		target_link_libraries(host_performance_counter_tests PRIVATE sgre_platform_render)
		set_target_properties(host_performance_counter_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_host_performance_counter COMMAND host_performance_counter_tests)
		add_executable(color_pass_tests tests/color_pass_tests.cpp)
		target_link_libraries(color_pass_tests PRIVATE sgre_platform_render)
		set_target_properties(color_pass_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_color_pass COMMAND color_pass_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/color_pass.metal")
		add_executable(motion_sprite_frame_metal_tests tests/motion_sprite_frame_metal_tests.cpp)
		target_link_libraries(motion_sprite_frame_metal_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(motion_sprite_frame_metal_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_motion_sprite_frame COMMAND motion_sprite_frame_metal_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_motion_sprite_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60)
		add_executable(sprite_pass_tests tests/sprite_pass_tests.cpp)
		target_link_libraries(sprite_pass_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_pass_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_pass COMMAND sprite_pass_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")

		add_executable(sprite_depth_clip_tests tests/sprite_depth_clip_tests.cpp)
		target_link_libraries(sprite_depth_clip_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_depth_clip_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_depth_clip COMMAND sprite_depth_clip_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		add_executable(mutable_texture_metal_tests tests/mutable_texture_metal_tests.cpp)
		target_link_libraries(mutable_texture_metal_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(mutable_texture_metal_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_mutable_texture COMMAND mutable_texture_metal_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		add_executable(raw_texture_pass_tests tests/raw_texture_pass_tests.cpp)
		target_link_libraries(raw_texture_pass_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(raw_texture_pass_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_raw_texture_pass COMMAND raw_texture_pass_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_BINARY_DIR}/raw_texture_without_v4.metal")
		add_executable(motion_copied_metal_tests tests/motion_copied_metal_tests.cpp)
		target_link_libraries(motion_copied_metal_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(motion_copied_metal_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_motion_copied_region COMMAND motion_copied_metal_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		foreach(test_name IN ITEMS texture_mask_live_frame motion_prepared_record_receiver motion_active_record_receiver bitmap_font_record_receiver)
			if(test_name STREQUAL "motion_prepared_record_receiver" OR test_name STREQUAL "motion_active_record_receiver")
				add_executable(${test_name}_tests tests/${test_name}_tests.cpp)
			else()
				add_executable(${test_name}_tests tests/${test_name}_tests.mm)
				target_compile_options(${test_name}_tests PRIVATE -fobjc-arc)
			endif()
			target_link_libraries(${test_name}_tests PRIVATE sgre_resource sgre_platform_render)
			set_target_properties(${test_name}_tests PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
			if(test_name STREQUAL "bitmap_font_record_receiver")
				add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
			else()
				add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
			endif()
			set_tests_properties(macos_${test_name} PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		endforeach()
		add_executable(system_mask_mode_tests tests/system_mask_mode_tests.cpp)
		target_link_libraries(system_mask_mode_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(system_mask_mode_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_system_mask_mode COMMAND system_mask_mode_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		add_executable(raw_texture_frame_cycle_tests tests/raw_texture_frame_cycle_tests.mm)
		target_include_directories(raw_texture_frame_cycle_tests PRIVATE src/platform/macos)
		target_compile_definitions(raw_texture_frame_cycle_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_compile_options(raw_texture_frame_cycle_tests PRIVATE -fobjc-arc)
		target_link_libraries(raw_texture_frame_cycle_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(raw_texture_frame_cycle_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_raw_texture_frame_cycle COMMAND raw_texture_frame_cycle_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_system_mask_mode macos_raw_texture_frame_cycle PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(copied_additive_metal_tests tests/copied_additive_metal_tests.cpp)
		target_link_libraries(copied_additive_metal_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(copied_additive_metal_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_copied_additive COMMAND copied_additive_metal_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_BINARY_DIR}/copied_additive_without_fragment.metal")
		add_executable(screen_driver_frame_tests tests/screen_driver_frame_tests.cpp)
		target_link_libraries(screen_driver_frame_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(screen_driver_frame_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_screen_driver_frame COMMAND screen_driver_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		add_executable(sprite_upload_slot_pacing_tests tests/sprite_upload_slot_pacing_tests.mm)
		target_include_directories(sprite_upload_slot_pacing_tests PRIVATE src/platform/macos)
		target_link_libraries(sprite_upload_slot_pacing_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(sprite_upload_slot_pacing_tests PRIVATE -fobjc-arc)
		set_target_properties(sprite_upload_slot_pacing_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_upload_slot_pacing COMMAND sprite_upload_slot_pacing_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_sprite_upload_slot_pacing PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(screen_live_frame_tests tests/screen_live_frame_tests.mm)
		target_include_directories(screen_live_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(screen_live_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(screen_live_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(screen_live_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(screen_live_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_screen_live_frame COMMAND screen_live_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_screen_live_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(text_glyph_frame_tests tests/text_glyph_frame_tests.mm)
		target_include_directories(text_glyph_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(text_glyph_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(text_glyph_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(text_glyph_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(text_glyph_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_text_glyph_frame COMMAND text_glyph_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_text_glyph_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(picture_frame_tests tests/picture_frame_tests.mm)
		target_include_directories(picture_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(picture_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(picture_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(picture_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(picture_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_picture_frame COMMAND picture_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_picture_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(picture_record_tests tests/picture_record_tests.mm)
		target_link_libraries(picture_record_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(picture_record_tests PRIVATE -fobjc-arc)
		set_target_properties(picture_record_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_picture_record COMMAND picture_record_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_picture_record PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(movie_frame_tests tests/movie_frame_tests.mm)
		target_include_directories(movie_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(movie_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(movie_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(movie_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(movie_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_movie_frame COMMAND movie_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_movie_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(captured_opacity_map_frame_tests tests/captured_opacity_map_frame_tests.mm)
		target_include_directories(captured_opacity_map_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(captured_opacity_map_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(captured_opacity_map_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(captured_opacity_map_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(captured_opacity_map_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_captured_opacity_map_frame COMMAND captured_opacity_map_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_captured_opacity_map_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(captured_frame_tests tests/captured_frame_tests.mm)
		target_include_directories(captured_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(captured_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(captured_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(captured_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(captured_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_captured_frame COMMAND captured_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_captured_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(capture_frame_tests tests/capture_frame_tests.mm)
		target_include_directories(capture_frame_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(capture_frame_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(capture_frame_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(capture_frame_tests PRIVATE -fobjc-arc)
		set_target_properties(capture_frame_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_capture_frame COMMAND capture_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_capture_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(mutable_staging_metal_tests tests/mutable_staging_metal_tests.mm)
		target_include_directories(mutable_staging_metal_tests PRIVATE src/platform/macos)
		target_link_libraries(mutable_staging_metal_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(mutable_staging_metal_tests PRIVATE -fobjc-arc)
		set_target_properties(mutable_staging_metal_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_mutable_staging COMMAND mutable_staging_metal_tests)
		set_tests_properties(macos_mutable_staging PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(fill_rectangle_render_tests tests/fill_rectangle_render_tests.mm)
		target_include_directories(fill_rectangle_render_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(fill_rectangle_render_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(fill_rectangle_render_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(fill_rectangle_render_tests PRIVATE -fobjc-arc)
		set_target_properties(fill_rectangle_render_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_fill_rectangle_render COMMAND fill_rectangle_render_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_fill_rectangle_render PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(screen_projection_live_tests tests/screen_projection_live_tests.mm)
		target_include_directories(screen_projection_live_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(screen_projection_live_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(screen_projection_live_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(screen_projection_live_tests PRIVATE -fobjc-arc)
		set_target_properties(screen_projection_live_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_screen_projection_live COMMAND screen_projection_live_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_screen_projection_live PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(native_texture_motion_live_tests tests/native_texture_motion_live_tests.mm)
		target_include_directories(native_texture_motion_live_tests PRIVATE tests src/platform/macos)
		target_compile_definitions(native_texture_motion_live_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(native_texture_motion_live_tests PRIVATE sgre_resource sgre_platform_render)
		target_compile_options(native_texture_motion_live_tests PRIVATE -fobjc-arc)
		set_target_properties(native_texture_motion_live_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_native_texture_motion_live COMMAND native_texture_motion_live_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_native_texture_motion_live PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		add_executable(screens_tests tests/screens_tests.cpp)
		target_link_libraries(screens_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(screens_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_screens COMMAND screens_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")


		add_executable(sprite_draw_transform_tests tests/sprite_draw_transform_tests.cpp)
		target_link_libraries(sprite_draw_transform_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_draw_transform_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_draw_transform COMMAND sprite_draw_transform_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_sprite_draw_transform PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(sprite_host_inputs_tests tests/sprite_host_inputs_tests.cpp)
		target_link_libraries(sprite_host_inputs_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_host_inputs_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_host_inputs COMMAND sprite_host_inputs_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/paged-sprite-inputs-stage/paged-inputs-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_sprite_host_inputs PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(live_metal_frame_tests tests/live_metal_frame_tests.cpp tests/live_frame_presentation_tests.mm)
		target_include_directories(live_metal_frame_tests PRIVATE src/platform/macos)
		target_compile_options(live_metal_frame_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
		target_link_libraries(live_metal_frame_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(live_metal_frame_tests PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_live_metal_frame COMMAND live_metal_frame_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_live_metal_frame PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(live_encoder_metadata_tests tests/live_encoder_metadata_tests.cpp)
		target_link_libraries(live_encoder_metadata_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(live_encoder_metadata_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_live_encoder_metadata COMMAND live_encoder_metadata_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_live_encoder_metadata PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(sprite_render_records_tests tests/sprite_render_records_tests.cpp)
		target_link_libraries(sprite_render_records_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_render_records_tests PROPERTIES CXX_EXTENSIONS OFF)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-render-record-owner-stage/native-records.bin")
			add_test(NAME macos_sprite_render_records COMMAND sprite_render_records_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-render-record-owner-stage/native-records.bin" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
			set_tests_properties(macos_sprite_render_records PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		endif()

		add_executable(shared_graphics_camera_tests tests/shared_graphics_camera_tests.cpp)
		target_link_libraries(shared_graphics_camera_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(shared_graphics_camera_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_shared_graphics_camera COMMAND shared_graphics_camera_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_shared_graphics_camera PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(graphics_default_state_tests tests/graphics_default_state_tests.cpp)
		target_link_libraries(graphics_default_state_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(graphics_default_state_tests PROPERTIES CXX_EXTENSIONS OFF)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-default-raster-stage/default-states-native.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-default-raster-stage/pipeline-cache-native.bin")
			add_test(NAME macos_graphics_default_state COMMAND graphics_default_state_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-default-raster-stage/default-states-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-default-raster-stage/pipeline-cache-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
			set_tests_properties(macos_graphics_default_state PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		endif()

		add_executable(sprite_graphics_camera_tests tests/sprite_graphics_camera_tests.cpp)
		target_link_libraries(sprite_graphics_camera_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_graphics_camera_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_graphics_camera COMMAND sprite_graphics_camera_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-camera-state-stage/camera-native.bin")
		set_tests_properties(macos_sprite_graphics_camera PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(sprite_graphics_targets_tests tests/sprite_graphics_targets_tests.cpp)
		target_link_libraries(sprite_graphics_targets_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(sprite_graphics_targets_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_sprite_graphics_targets COMMAND sprite_graphics_targets_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-current-target-copy-stage/mono-target-selection-native.bin")
		set_tests_properties(macos_sprite_graphics_targets PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(display_sync_tests
			src/render/presentation_synchronization.cpp
			src/platform/macos/sprite_pass.mm
			src/platform/macos/sprite_presentation.mm
			src/platform/macos/metal_support.mm
			src/platform/macos/mutable_texture_backend.mm
			src/platform/macos/motion_sprite_frame.mm
			src/platform/macos/host_performance_counter.cpp
			src/platform/macos/tests/display_sync_tests.mm
		)
		target_include_directories(display_sync_tests PRIVATE include src/platform/macos)
		target_compile_definitions(display_sync_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_compile_options(display_sync_tests PRIVATE -fobjc-arc)
		target_link_libraries(display_sync_tests PRIVATE sgre_resource "-framework Foundation" "-framework Metal" "-framework AppKit" "-framework QuartzCore" "-framework Security")
		set_target_properties(display_sync_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_display_synchronization COMMAND display_sync_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/display-synchronization-stage/native-display-sync.bin" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_display_synchronization PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(shared_sprite_resources_tests tests/shared_sprite_resources_tests.cpp)
		target_link_libraries(shared_sprite_resources_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(shared_sprite_resources_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_shared_sprite_resources COMMAND shared_sprite_resources_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_shared_sprite_resources PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		# Fixture hooks require one directly compiled presentation/renderer set; production excludes them.
		add_executable(application_pump_tests
			src/render/presentation_synchronization.cpp
			src/platform/macos/application_pump.mm
			src/platform/macos/hid_controller_presence.mm
			src/platform/macos/sprite_pass.mm
			src/platform/macos/sprite_presentation.mm
			src/platform/macos/metal_support.mm
			src/platform/macos/mutable_texture_backend.mm
			src/platform/macos/motion_sprite_frame.mm
			src/platform/macos/host_performance_counter.cpp
			src/platform/macos/tests/application_pump_tests.mm
		)
		target_include_directories(application_pump_tests PRIVATE include src/platform/macos)
		target_compile_definitions(application_pump_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_link_libraries(application_pump_tests PRIVATE sgre_resource sgre_platform_render "-framework Foundation" "-framework Metal" "-framework AppKit" "-framework QuartzCore" "-framework Security")
		target_compile_options(application_pump_tests PRIVATE -fobjc-arc)
		set_target_properties(application_pump_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_application_pump_prefix COMMAND application_pump_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_application_pump_prefix PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)

		add_executable(keyboard_sample_tests src/platform/macos/tests/keyboard_sample_tests.cpp)
		target_include_directories(keyboard_sample_tests PRIVATE src/platform/macos)
		target_link_libraries(keyboard_sample_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(keyboard_sample_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_keyboard_sample COMMAND keyboard_sample_tests)

		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/startup-dependency-observer-stage/override.sqbc")
			add_executable(application_finish_pump_tests src/platform/macos/tests/application_finish_pump_tests.mm)
			target_include_directories(application_finish_pump_tests PRIVATE tests src/platform/macos)
			target_compile_definitions(application_finish_pump_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
			target_compile_options(application_finish_pump_tests PRIVATE -fobjc-arc)
			target_link_libraries(application_finish_pump_tests PRIVATE sgre_resource sgre_platform_render)
			set_target_properties(application_finish_pump_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
			add_test(NAME macos_application_finish_pump COMMAND application_finish_pump_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/startup-dependency-observer-stage/override.sqbc")
			set_tests_properties(macos_application_finish_pump PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 30 RUN_SERIAL TRUE)
		endif()

		add_executable(screen_retirement_order_tests tests/screen_retirement_order_tests.cpp)
		target_link_libraries(screen_retirement_order_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(screen_retirement_order_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_screen_retirement_order COMMAND screen_retirement_order_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
	endif()

	if(SGRE_GAME_ROOT)
		if(NOT SGRE_SEED_FILE)
			message(FATAL_ERROR "Local integration checks require SGRE_SEED_FILE")
		endif()

		add_executable(local_squirrel_string_tests tests/local_squirrel_string_tests.cpp)
		target_link_libraries(local_squirrel_string_tests PRIVATE sgre_resource)
		set_target_properties(local_squirrel_string_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME local_squirrel_strings COMMAND local_squirrel_string_tests "${SGRE_GAME_ROOT}" "${SGRE_SEED_FILE}")
		add_executable(local_archive_tests tests/local_archive_tests.cpp)
		target_link_libraries(local_archive_tests PRIVATE sgre_resource)
		set_target_properties(local_archive_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME local_archive_reader COMMAND local_archive_tests "${SGRE_GAME_ROOT}" "${SGRE_SEED_FILE}")
		add_executable(local_resource_binary_tests tests/local_resource_binary_tests.cpp)
		target_link_libraries(local_resource_binary_tests PRIVATE sgre_resource)
		set_target_properties(local_resource_binary_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME local_resource_binary COMMAND local_resource_binary_tests "${SGRE_GAME_ROOT}" "${SGRE_SEED_FILE}")
		add_executable(local_psb_jobs_tests tests/local_psb_jobs_tests.cpp)
		target_link_libraries(local_psb_jobs_tests PRIVATE sgre_resource)
		set_target_properties(local_psb_jobs_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME local_psb_jobs COMMAND local_psb_jobs_tests "${SGRE_GAME_ROOT}" "${SGRE_SEED_FILE}")
		add_executable(local_script_startup_tests tests/local_script_startup_tests.cpp)
		target_link_libraries(local_script_startup_tests PRIVATE sgre_resource)
		if(SGRE_BUILD_MACOS_RENDERER)
			target_compile_definitions(local_script_startup_tests PRIVATE SGRE_HAS_MACOS_RENDERER)
			target_link_libraries(local_script_startup_tests PRIVATE sgre_platform_render)
		endif()
		set_target_properties(local_script_startup_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME local_script_startup COMMAND local_script_startup_tests "${SGRE_GAME_ROOT}" "${SGRE_SEED_FILE}")
		if(SGRE_BUILD_MACOS_RENDERER)
			add_test(NAME local_platform_startup COMMAND local_script_startup_tests "${SGRE_GAME_ROOT}" "${SGRE_SEED_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		endif()

		foreach(archive_name IN ITEMS config font image map motion scenario script sound voice)
			add_test(NAME local_index_${archive_name} COMMAND sgre-inspect index "${SGRE_GAME_ROOT}/wind3d11data/${archive_name}_info.psb.m" --body "${SGRE_GAME_ROOT}/wind3d11data/${archive_name}_body.bin" --seed-file "${SGRE_SEED_FILE}")
		endforeach()

		foreach(archive_name IN ITEMS sound voice)
			add_test(NAME local_scan_${archive_name} COMMAND sgre-inspect scan "${SGRE_GAME_ROOT}/wind3d11data/${archive_name}_body.bin")
		endforeach()
	endif()
endif()


if(SGRE_BUILD_TESTS AND SGRE_BUILD_MACOS_RENDERER)
	set(SGRE_GRAPHICS_FRAME_TESTS graphics_motion_frame_globals graphics_mask_control graphics_record_preloop_reset motion_sprite_record_receiver motion_sprite_pool_receiver)
	foreach(test_name IN LISTS SGRE_GRAPHICS_FRAME_TESTS)
		add_executable(${test_name}_tests tests/${test_name}_tests.cpp)
		target_link_libraries(${test_name}_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(${test_name}_tests PROPERTIES CXX_EXTENSIONS OFF)
		if(test_name STREQUAL "motion_sprite_pool_receiver")
			add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		elseif(test_name STREQUAL "graphics_mask_control")
			if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-ordinary-mask-control-stage/native-mask-control.bin")
				add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/graphics-ordinary-mask-control-stage/native-mask-control.bin" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
			endif()
		else()
			add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		endif()
		if(TEST macos_${test_name})
			set_tests_properties(macos_${test_name} PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
		endif()
	endforeach()

	set(SGRE_GRAPHICS_POOL_TESTS presentation_capacity frame_host_pool frame_host_pool_writer graphics_frame_cycle graphics_display_frame motion_scene_repeat_draw)
	foreach(test_name IN LISTS SGRE_GRAPHICS_POOL_TESTS)
		if(test_name STREQUAL "graphics_frame_cycle" OR test_name STREQUAL "graphics_display_frame" OR test_name STREQUAL "motion_scene_repeat_draw")
			add_executable(${test_name}_tests tests/${test_name}_tests.mm)
		else()
			add_executable(${test_name}_tests src/platform/macos/tests/${test_name}_tests.mm)
		endif()
		target_include_directories(${test_name}_tests PRIVATE src/platform/macos tests)
		target_compile_definitions(${test_name}_tests PRIVATE SGRE_SPRITE_PRESENTATION_FIXTURE=1)
		target_compile_options(${test_name}_tests PRIVATE -fobjc-arc)
		target_link_libraries(${test_name}_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(${test_name}_tests PROPERTIES CXX_EXTENSIONS OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
		add_test(NAME macos_${test_name} COMMAND ${test_name}_tests "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_presentation.metal")
		set_tests_properties(macos_${test_name} PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
	endforeach()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-layer-binding-stage/parameter-layer-local-native.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-local-finish-stage/parameter-index-native.bin" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-local-finish-empty-child-correction-stage/parameter-seek-native.bin")
		add_executable(motion_parameter_local_finish_tests tests/motion_parameter_local_finish_tests.cpp)
		target_link_libraries(motion_parameter_local_finish_tests PRIVATE sgre_resource sgre_platform_render)
		set_target_properties(motion_parameter_local_finish_tests PROPERTIES CXX_EXTENSIONS OFF)
		add_test(NAME macos_motion_parameter_local_finish COMMAND motion_parameter_local_finish_tests "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-layer-binding-stage/parameter-layer-local-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-local-finish-stage/parameter-index-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/Files/research/runtime/selected-parameter-local-finish-empty-child-correction-stage/parameter-seek-native.bin" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/sprite_pass.metal")
		set_tests_properties(macos_motion_parameter_local_finish PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60 RUN_SERIAL TRUE)
	endif()
endif()

find_program(SGRE_CLANG_FORMAT NAMES clang-format HINTS /opt/homebrew/opt/llvm/bin)
if(SGRE_CLANG_FORMAT)
	file(GLOB_RECURSE SGRE_FORMAT_SOURCES CONFIGURE_DEPENDS include/*.hpp src/*.hpp src/*.cpp src/*.mm tools/*.cpp tests/*.cpp tests/*.hpp tests/*.mm shaders/*.hlsl shaders/*.metal)
	add_custom_target(sgre-check-format
		COMMAND ${SGRE_CLANG_FORMAT} --dry-run --Werror ${SGRE_FORMAT_SOURCES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		VERBATIM
	)
endif()
