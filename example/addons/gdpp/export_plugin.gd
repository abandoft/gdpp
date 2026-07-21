@tool
extends EditorExportPlugin

const OUTPUT_DIRECTORY := "res://addons/gdpp/build/project"
const BINARY_DIRECTORY := "res://addons/gdpp/binary"
const COMPILER_DESCRIPTOR := "res://addons/gdpp/gdpp.gdextension"
const LEGACY_RUNTIME_DESCRIPTOR := "res://addons/gdpp/gdpp_project.gdextension"
const ADDON_PREFIX := "res://addons/gdpp/"
const RUNTIME_RESOURCE_PREFIX := "res://addons/gdpp/runtime/"
const COMPILER_SETTING := "gdpp/build/cpp_compiler"
const SDK_SETTING := "gdpp/build/sdk_root"
const ANDROID_NDK_SETTING := "gdpp/build/android_ndk_root"
const EMSCRIPTEN_CXX_SETTING := "gdpp/build/emscripten_cxx"
const TARGET_VERSION_SETTING := "gdpp/target_godot_version"
const STRIP_OPTION := "gdpp/strip_gdscript_sources"
const ALLOW_SOURCE_FALLBACK_OPTION := "gdpp/allow_source_fallback"
const EXTENSION_REGISTRY := "res://.godot/extension_list.cfg"
const EXTENSION_REGISTRY_BACKUP := "res://.godot/gdpp_extension_list.export-backup"
const COMPILER_DESCRIPTOR_BACKUP := "res://.godot/gdpp_compiler_descriptor.export-backup"
const PROVIDER_DESCRIPTORS_BACKUP := (
    "res://.godot/gdpp_provider_descriptors.export-backup.json"
)
const SCRIPT_CLASS_CACHE := "res://.godot/global_script_class_cache.cfg"
const GODOT_EXPORT_CACHE_DIRECTORY := "res://.godot/exported"
const EXPORT_TRANSFORM_REVISION := 18

var _compiler: Object
var _ready := false
var _script_classes: Dictionary = {}
var _attached_script_bases: Dictionary = {}
var _compiled_scripts: Dictionary = {}
var _abstract_scripts: Dictionary = {}
var _editor_only_scripts: Dictionary = {}
var _runtime_descriptor := ""
var _export_scan_descriptor := ""
var _output_library := ""
var _build_id := ""
var _target_platform := ""
var _target_architecture := ""
var _target_variant := ""
var _build_profile := ""
var _export_extension_registry := ""
var _export_script_class_cache := ""
var _include_project_extension := false
var _extension_registry_original := ""
var _extension_registry_modified := false
var _compiler_descriptor_original := ""
var _compiler_descriptor_modified := false
var _provider_descriptor_originals: Dictionary = {}
var _has_resource_scripts := false
var _export_output_path := ""
var _autoload_files: Dictionary = {}
var _autoload_replacements: Dictionary = {}
var _autoload_originals: Dictionary = {}
var _metrics_mutex := Mutex.new()
var _customized_scene_count := 0
var _replaced_node_count := 0
var _customized_resource_count := 0
var _copied_property_count := 0
var _strict_failure_injected := false


func configure(compiler: Object) -> void:
    _compiler = compiler


func _get_name() -> String:
    # Godot 4.5 mixes a scene customizer's name into the on-disk export-cache
    # directory but accidentally leaves its scene configuration hash out of
    # that directory key. Include the transformation revision and native build
    # ID in the name as a compatibility workaround so a repaired compiler or
    # exporter can never reuse scenes cached by an older transformation.
    return "GDPP AOT scene transformer r%d %s" % [
        EXPORT_TRANSFORM_REVISION,
        _build_id,
    ]


func _supports_platform(platform: EditorExportPlatform) -> bool:
    var os_name := platform.get_os_name().to_lower()
    return (
        os_name == "macos"
        or os_name == "windows"
        or os_name == "linux"
        or os_name == "android"
        or os_name == "ios"
        or os_name == "web"
    )


func _get_export_options(_platform: EditorExportPlatform) -> Array[Dictionary]:
    return [
        {
            "option": {
                "name": STRIP_OPTION,
                "type": TYPE_BOOL,
                "usage": PROPERTY_USAGE_DEFAULT,
            },
            "default_value": true,
        },
        {
            "option": {
                "name": ALLOW_SOURCE_FALLBACK_OPTION,
                "type": TYPE_BOOL,
                "usage": PROPERTY_USAGE_DEFAULT,
            },
            "default_value": false,
        },
    ]


func _export_begin(
    features: PackedStringArray,
    is_debug: bool,
    path: String,
    _flags: int
) -> void:
    # Recover a previous export transaction if Godot aborted before _export_end().
    _restore_export_transaction()
    _reset_export_state()
    _export_output_path = path
    _target_platform = _platform_from_features(features)
    _target_architecture = _architecture_from_features(features)
    _target_variant = _web_variant_from_features(features)
    _build_profile = "debug" if is_debug else "release"
    if get_option(STRIP_OPTION) != true:
        print("GDPP: AOT source stripping is disabled for this export preset")
        # Godot 4.5 can reuse a remapped scene produced by an earlier AOT preset even when the
        # current preset disables transformation. Such a scene names GDPPNative classes that are
        # deliberately absent from the pure-GDScript package. Invalidate the generated export
        # cache before file enumeration so fallback exports always start from project sources.
        if not _clear_godot_export_cache():
            _fail_export("cannot clear stale native scene export cache")
            return
        # This is an intentional pure-GDScript export, not a failed AOT build. Do not load the
        # empty fallback extension or retain any project-native library in the comparison/package.
        # Godot scans every .gdextension before _export_file() can skip it, so present the tiny
        # fallback only during that scan and remove the copied image at transaction end.
        _activate_fallback_descriptors(is_debug)
        _prepare_extension_registry(false)
        return
    if not _prepare_export(features, is_debug):
        _activate_fallback_descriptors(is_debug)
        _prepare_extension_registry(_target_platform in ["macos", "windows", "linux"])
        return

    if not _prepare_script_class_cache():
        _activate_fallback_descriptors(is_debug)
        # Android and Web have no host-loadable fallback library. Keeping a
        # project descriptor after a failed AOT transaction would make Godot
        # package a missing or wrong-ABI artifact instead of failing closed.
        _prepare_extension_registry(_target_platform in ["macos", "windows", "linux"])
        return
    if not _prepare_extension_registry():
        return
    _activate_autoloads()

    # The temporary scan descriptor is the single source of truth for native-library discovery.
    # Registering the same file through add_shared_object() as well makes Godot's Android exporter
    # append duplicate ZIP entries (and can multiply a large customer library threefold).
    _ready = true
    print(
        "GDPP: %s %s/%s project library is ready for binary-only export" % [
            _build_profile,
            _target_platform,
            _target_architecture,
        ]
    )


func _export_end() -> void:
    _restore_autoloads()
    _remove_successful_export_fallback()
    _restore_export_transaction()
    _print_export_summary()
    _reset_export_state()


func _begin_customize_scenes(
    _platform: EditorExportPlatform,
    _features: PackedStringArray
) -> bool:
    # See _begin_customize_resources(): Godot 4.4/4.5 feeds every exported
    # path through ResourceLoader as soon as either customization pass is
    # enabled. _export_file() performs selective scene transformation instead.
    return false


func _begin_customize_resources(
    _platform: EditorExportPlatform,
    _features: PackedStringArray
) -> bool:
    # Godot 4.4/4.5 applies resource customization to every exported file,
    # including raw files that ResourceLoader cannot load (for example .ico,
    # .md and .txt). Script-backed .tres/.res files are transformed selectively
    # in _export_file(), and embedded Resources are transformed while their
    # owning scene is copied, so the global resource pass is neither necessary
    # nor safe for a production export.
    return false


func _get_customization_configuration_hash() -> int:
    return hash([
        EXPORT_TRANSFORM_REVISION,
        _build_id,
        _target_platform,
        _target_architecture,
        _target_variant,
        _build_profile,
    ])


func _customize_scene(scene: Node, path: String) -> Node:
    var packed_scene := ResourceLoader.load(path, "PackedScene") as PackedScene
    if packed_scene == null:
        _fail_export("scene '%s' has no readable PackedScene state" % path)
        return null
    var resource_replacements: Dictionary = {}
    var resource_changes := {"count": 0}
    return _transform_scene_with_state(
        scene,
        packed_scene.get_state(),
        path,
        resource_replacements,
        resource_changes
    )


func _transform_scene_with_state(
    scene: Node,
    scene_state: SceneState,
    path: String,
    resource_replacements: Dictionary,
    resource_changes: Dictionary
) -> Node:
    var resource_change_start := int(resource_changes.get("count", 0))
    var replacement_plan: Array[Dictionary] = []
    if not _collect_scene_replacement_plan(scene, scene_state, path, replacement_plan):
        _fail_export("scene '%s' contains a script that cannot be replaced safely" % path)
        return null

    var replacement_nodes: Dictionary = {}
    var connection_snapshots := _snapshot_connections(scene, scene_state)
    var result := _replace_planned_nodes(
        scene,
        replacement_plan,
        replacement_nodes,
        resource_replacements,
        resource_changes,
        path
    )
    if result == null:
        _restore_connections(connection_snapshots, {})
        _fail_export("scene '%s' could not be replaced atomically" % path)
        return null
    _restore_connections(connection_snapshots, replacement_nodes)
    _transform_serialized_scene_resources(
        result,
        scene_state,
        path,
        resource_replacements,
        resource_changes
    )
    if _strict_failure_injected:
        result.free()
        return null
    if (
        replacement_plan.is_empty()
        and int(resource_changes.count) == resource_change_start
    ):
        return null
    _record_scene_metrics(
        replacement_plan.size(),
        int(replacement_nodes.get("__gdpp_copied_properties", 0))
    )
    return result


func _customize_resource(resource: Resource, path: String) -> Resource:
    var script_path := _script_path(resource)
    if script_path.is_empty():
        return null
    if _editor_only_scripts.has(script_path):
        _fail_export("runtime resource '%s' uses editor-only script '%s'" % [path, script_path])
        return null
    if not _script_classes.has(script_path):
        _fail_export("resource '%s' uses an uncompiled script '%s'" % [path, script_path])
        return null
    if _attached_script_bases.has(script_path):
        var replacements := {resource.get_instance_id(): resource}
        var changes := {"count": 0}
        var copied_properties := _attach_compiled_script(
            resource,
            script_path,
            null,
            replacements,
            changes,
            path
        )
        if copied_properties < 0:
            _fail_export("resource '%s' cannot attach its compiled script" % path)
            return null
        _metrics_mutex.lock()
        _customized_resource_count += 1
        _copied_property_count += copied_properties
        _metrics_mutex.unlock()
        return resource
    var replacement := ClassDB.instantiate(StringName(_script_classes[script_path]))
    if not replacement is Resource:
        if replacement != null:
            replacement.free()
        _fail_export("native class for '%s' is not a Resource" % script_path)
        return null
    var copied_properties := _copy_storage_properties(resource, replacement)
    if copied_properties < 0:
        replacement.free()
        _fail_export("resource '%s' cannot preserve all stored properties" % path)
        return null
    _metrics_mutex.lock()
    _customized_resource_count += 1
    _copied_property_count += copied_properties
    _metrics_mutex.unlock()
    return replacement


func _export_file(path: String, _type: String, _features: PackedStringArray) -> void:
    if _strict_failure_injected:
        # Fail closed even on Godot versions whose command-line exporter does
        # not convert EXPORT_MESSAGE_ERROR into a non-zero process exit. No
        # customer resource or script is allowed into the failed package.
        skip()
        return

    if path == LEGACY_RUNTIME_DESCRIPTOR:
        skip()
        return

    # Keep one physical descriptor path in both editor and export. During export its contents are
    # transactionally rewritten for native-library discovery, while the package receives the
    # runtime descriptor bytes at that same path. Godot's forced extension-list filter only keeps
    # physical project paths; a virtual second path is silently removed on Godot 4.6+.
    if path == COMPILER_DESCRIPTOR:
        if _include_project_extension and not _runtime_descriptor.is_empty():
            add_file(COMPILER_DESCRIPTOR, _runtime_descriptor.to_utf8_buffer(), false)
        skip()
        return

    if path == EXTENSION_REGISTRY and not _export_extension_registry.is_empty():
        add_file(path, _export_extension_registry.to_utf8_buffer(), false)
        skip()
        return

    # macOS Universal 2 export temporarily normalizes third-party descriptors for Godot's
    # architecture scanner. Ship the provider's byte-for-byte original runtime descriptor.
    if _provider_descriptor_originals.has(path):
        add_file(path, str(_provider_descriptor_originals[path]).to_utf8_buffer(), false)
        skip()
        return

    if path == SCRIPT_CLASS_CACHE and _ready and not _export_script_class_cache.is_empty():
        add_file(path, _export_script_class_cache.to_utf8_buffer(), false)
        skip()
        return

    if path.begins_with(ADDON_PREFIX):
        skip()
        return

    if _ready and path.get_extension().to_lower() in ["tscn", "scn"]:
        if _export_transformed_scene(path):
            return
        if _strict_failure_injected:
            skip()
            return

    if _ready and _has_resource_scripts and path.get_extension().to_lower() in ["tres", "res"]:
        if _export_transformed_resource(path):
            return
        if _strict_failure_injected:
            skip()
            return

    var source_path := path.trim_suffix("c") if path.ends_with(".gdc") else path
    if _ready and _compiled_scripts.has(source_path):
        skip()


func _export_transformed_scene(path: String) -> bool:
    var packed := ResourceLoader.load(
        path,
        "PackedScene",
        ResourceLoader.CACHE_MODE_IGNORE
    ) as PackedScene
    if packed == null:
        _fail_export("scene '%s' cannot be loaded for AOT transformation" % path)
        return false
    var source_root := packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
    if source_root == null:
        _fail_export("scene '%s' cannot be instantiated for AOT transformation" % path)
        return false
    var transformed_root := _customize_scene(source_root, path)
    if transformed_root == null:
        if is_instance_valid(source_root):
            source_root.free()
        return false

    var transformed_scene := PackedScene.new()
    var pack_error := transformed_scene.pack(transformed_root)
    if pack_error != OK:
        transformed_root.free()
        _fail_export("cannot pack transformed scene '%s' (error %d)" % [path, pack_error])
        return false
    var result := _serialize_export_resource(
        transformed_scene,
        path,
        "scenes",
        "scn"
    )
    transformed_root.free()
    return result


func _export_transformed_resource(path: String) -> bool:
    var source := ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
    if source == null:
        _fail_export("resource '%s' cannot be loaded for AOT transformation" % path)
        return false
    var replacements: Dictionary = {}
    var changes := {"count": 0}
    var transformed := _transform_resource_graph(source, path, replacements, changes, true)
    if transformed == null:
        return false
    if int(changes.count) == 0:
        return false
    return _serialize_export_resource(transformed, path, "resources", "res")


func _serialize_export_resource(
    resource: Resource,
    source_path: String,
    runtime_directory: String,
    extension: String
) -> bool:
    var cache_directory := OUTPUT_DIRECTORY.path_join("export-cache")
    var cache_absolute := ProjectSettings.globalize_path(cache_directory)
    var directory_error := DirAccess.make_dir_recursive_absolute(cache_absolute)
    if directory_error != OK and directory_error != ERR_ALREADY_EXISTS:
        _fail_export(
            "cannot create the native resource export cache '%s' (error %d)"
            % [cache_directory, directory_error]
        )
        return false
    var cache_path := cache_directory.path_join("%s.%s" % [source_path.md5_text(), extension])
    var save_error := ResourceSaver.save(resource, cache_path)
    if save_error != OK:
        _fail_export(
            "cannot serialize transformed resource '%s' (error %d)"
            % [source_path, save_error]
        )
        return false
    var bytes := FileAccess.get_file_as_bytes(cache_path)
    var read_error := FileAccess.get_open_error()
    DirAccess.remove_absolute(ProjectSettings.globalize_path(cache_path))
    if read_error != OK or bytes.is_empty():
        _fail_export(
            "cannot read transformed resource '%s' (error %d)" % [source_path, read_error]
        )
        return false

    var runtime_path := RUNTIME_RESOURCE_PREFIX.path_join(
        "%s/%s-%s.%s" % [
            runtime_directory,
            _build_id.left(12),
            source_path.md5_text(),
            extension,
        ]
    )
    # `remap = true` replaces only the current scene/resource file. Unlike the
    # global resource customizer this never asks Godot to deserialize arbitrary
    # customer assets, and the generated binary Resource contains no GDScript.
    add_file(runtime_path, bytes, true)
    return true


func _prepare_export(features: PackedStringArray, is_debug: bool) -> bool:
    if _compiler == null:
        _fail_export("compiler service is not available; keeping GDScript sources")
        return false

    _target_platform = _platform_from_features(features)
    _target_architecture = _architecture_from_features(features)
    _target_variant = _web_variant_from_features(features)
    if _target_platform.is_empty() or _target_architecture.is_empty():
        _fail_export(
            "unsupported or ambiguous export target features: %s; keeping GDScript sources"
            % [", ".join(features)]
        )
        return false
    if _target_platform == "web":
        if _target_variant.is_empty():
            _fail_export(
                "Web export must select exactly one thread mode (threads or nothreads)"
            )
            return false
        var preset := get_export_preset()
        if preset == null or preset.get("variant/extensions_support") != true:
            _fail_export(
                "Web AOT requires the export preset option 'Variant > Extensions Support'"
            )
            return false
    _build_profile = "debug" if is_debug else "release"

    var target_version := str(ProjectSettings.get_setting(TARGET_VERSION_SETTING, "4.4"))
    var sdk_root := str(ProjectSettings.get_setting(SDK_SETTING, ""))
    var cpp_compiler := str(ProjectSettings.get_setting(COMPILER_SETTING, ""))

    var development_result: Dictionary = _compiler.compile_project(
        "res://",
        OUTPUT_DIRECTORY,
        sdk_root,
        cpp_compiler,
        target_version,
        "development",
        _compiler.get_host_platform(),
        _compiler.get_host_architecture()
    )
    if not _execute_build(development_result, "development"):
        return false
    var development_library := str(development_result.get("output_library", ""))
    if development_library.is_empty() or not _native_artifact_exists(development_library):
        _fail_export("native development library was not produced: %s" % development_library)
        return false
    var development_classes: Dictionary = development_result.get("script_classes", {})
    if _native_classes_are_loaded(development_classes):
        print("GDPP: matching development native classes are already loaded")
        _cleanup_stale_development_libraries(development_library)
    elif not _load_development_project_extension(development_library):
        return false

    var distribution_compiler := cpp_compiler
    if _target_platform == "android":
        distribution_compiler = _android_compiler()
        if distribution_compiler.is_empty():
            return false
    elif _target_platform == "ios":
        distribution_compiler = _ios_compiler()
        if distribution_compiler.is_empty():
            return false
    elif _target_platform == "web":
        distribution_compiler = _web_compiler()
        if distribution_compiler.is_empty():
            return false

    var distribution_result: Dictionary = _compiler.compile_project(
        "res://",
        OUTPUT_DIRECTORY,
        sdk_root,
        distribution_compiler,
        target_version,
        _build_profile,
        _target_platform,
        _target_architecture,
        _target_variant
    )
    if not _execute_build(distribution_result, _build_profile):
        return false

    _script_classes = distribution_result.get("script_classes", {})
    _attached_script_bases = distribution_result.get("attached_script_bases", {})
    _compiled_scripts.clear()
    for script_path: String in _script_classes:
        _compiled_scripts[script_path] = true
    _abstract_scripts.clear()
    for script_path: String in distribution_result.get("abstract_scripts", PackedStringArray()):
        _abstract_scripts[script_path] = true
    _editor_only_scripts.clear()
    for script_path: String in distribution_result.get(
        "editor_only_scripts", PackedStringArray()
    ):
        _editor_only_scripts[script_path] = true
    _build_id = str(distribution_result.get("build_id", ""))
    _output_library = str(distribution_result.get("output_library", ""))
    if not _native_artifact_exists(_output_library):
        _fail_export("native distribution library was not produced: %s" % _output_library)
        return false
    if not _validate_native_classes():
        return false
    if not _prepare_autoloads():
        return false

    var library_path := "res://addons/gdpp/binary/%s" % _output_library.get_file()
    _runtime_descriptor = (
        "[configuration]\n\n"
        + "entry_symbol = \"gdpp_project_library_init\"\n"
        + "compatibility_minimum = \"%s\"\n" % target_version
        + "reloadable = false\n\n"
        + "[libraries]\n\n"
        + _runtime_library_entries(
            library_path,
            "threads" if _target_platform == "web" and _target_variant == "threads" else ""
        )
    )
    _export_scan_descriptor = (
        "[configuration]\n\n"
        + "entry_symbol = \"gdpp_project_library_init\"\n"
        + "compatibility_minimum = \"%s\"\n" % target_version
        + "reloadable = false\n\n"
        + "[libraries]\n\n"
        + _export_scan_library_entries(
            library_path,
            "threads" if _target_platform == "web" and _target_variant == "threads" else ""
        )
    )
    return true


func _execute_build(result: Dictionary, label: String) -> bool:
    var execution: Dictionary = _compiler.execute_project_build(result)
    if not execution.get("success", false):
        for diagnostic in execution.get("diagnostics", []):
            _fail_export("%s build: %s" % [label, diagnostic])
        return false
    for diagnostic in execution.get("diagnostics", []):
        push_warning("GDPP: %s build: %s" % [label, diagnostic])
    var removed := int(execution.get("removed_count", 0))
    if removed > 0:
        print("GDPP: removed %d stale development project libraries" % removed)
    return true


func _load_development_project_extension(current_library: String) -> bool:
    var descriptor := OUTPUT_DIRECTORY + "/gdpp_project.gdextension"
    var expected_resource_path := "res://addons/gdpp/binary/%s" % current_library.get_file()
    var descriptor_text := FileAccess.get_file_as_string(descriptor)
    if descriptor_text.is_empty() or not descriptor_text.contains(
        '= "%s"' % expected_resource_path
    ):
        _fail_export(
            (
                "development extension descriptor does not reference the freshly built library "
                + "'%s'; refusing an unsafe GDExtension load"
            )
            % expected_resource_path
        )
        return false
    if GDExtensionManager.is_extension_loaded(descriptor):
        var unload_status := GDExtensionManager.unload_extension(descriptor)
        if unload_status != GDExtensionManager.LOAD_STATUS_OK:
            _fail_export(
                (
                    "cannot unload the previous development project extension (status %d); "
                    + "close native project instances or restart Godot"
                )
                % unload_status
            )
            return false
    var status := GDExtensionManager.load_extension(descriptor)
    if status not in [
        GDExtensionManager.LOAD_STATUS_OK,
        GDExtensionManager.LOAD_STATUS_ALREADY_LOADED,
    ]:
        _fail_export("cannot load generated development extension (status %d)" % status)
        return false
    _cleanup_stale_development_libraries(current_library)
    return true


func _cleanup_stale_development_libraries(current_library: String) -> void:
    var cleanup: Dictionary = _compiler.prune_stale_development_libraries(current_library)
    for diagnostic in cleanup.get("diagnostics", []):
        push_warning("GDPP: %s" % diagnostic)
    var removed := int(cleanup.get("removed_count", 0))
    if removed > 0:
        print("GDPP: removed %d stale development project libraries" % removed)


func _validate_native_classes() -> bool:
    _has_resource_scripts = false
    if not _attached_script_bases.is_empty():
        if not ClassDB.class_exists(&"AttachedCompiledScript"):
            _fail_export("attached script runtime class is unavailable")
            return false
    for script_path: String in _script_classes:
        if _editor_only_scripts.has(script_path):
            continue
        var native_class_name := StringName(_script_classes[script_path])
        if not ClassDB.class_exists(native_class_name):
            _fail_export(
                "native class '%s' for '%s' is unavailable" % [native_class_name, script_path]
            )
            return false
        if _attached_script_bases.has(script_path):
            var attached_base := StringName(_attached_script_bases[script_path])
            if not ClassDB.class_exists(attached_base):
                _fail_export(
                    "third-party native base '%s' for '%s' is unavailable" % [
                        attached_base,
                        script_path,
                    ]
                )
                return false
            if (
                not _abstract_scripts.has(script_path)
                and not ClassDB.can_instantiate(attached_base)
            ):
                _fail_export(
                    "third-party native base '%s' for '%s' cannot be instantiated" % [
                        attached_base,
                        script_path,
                    ]
                )
                return false
            if ClassDB.is_parent_class(attached_base, &"Resource"):
                _has_resource_scripts = true
            continue
        if (
            not _abstract_scripts.has(script_path)
            and not ClassDB.can_instantiate(native_class_name)
        ):
            _fail_export(
                "native class '%s' for '%s' cannot be instantiated" % [
                    native_class_name,
                    script_path,
                ]
            )
            return false
        if ClassDB.is_parent_class(native_class_name, &"Resource"):
            _has_resource_scripts = true
    return true


func _native_classes_are_loaded(classes: Dictionary) -> bool:
    if classes.is_empty():
        return false
    for script_path: String in classes:
        if not ClassDB.class_exists(StringName(classes[script_path])):
            return false
    return true


func _prepare_autoloads() -> bool:
    for property: Dictionary in ProjectSettings.get_property_list():
        var setting := str(property.get("name", ""))
        if not setting.begins_with("autoload/"):
            continue
        var original := str(ProjectSettings.get_setting(setting, ""))
        var script_path := original.trim_prefix("*")
        if not _compiled_scripts.has(script_path):
            continue
        if _editor_only_scripts.has(script_path):
            _fail_export("autoload '%s' uses an editor-only script" % setting)
            return false
        var native_class := StringName(_script_classes[script_path])
        var attached_base := StringName(_attached_script_bases.get(script_path, ""))
        # Export preparation must not construct customer autoloads: their field
        # initializers may access services that are only valid after SceneTree startup.
        var scene_type := attached_base if not attached_base.is_empty() else native_class
        if not ClassDB.is_parent_class(scene_type, &"Node"):
            _fail_export(
                "script autoload '%s' must compile to a Node-derived native class"
                % setting.trim_prefix("autoload/")
            )
            return false
        var autoload_name := setting.trim_prefix("autoload/")
        var generated_path := RUNTIME_RESOURCE_PREFIX + "autoload/%s.tscn" % setting.md5_text()
        var scene := ""
        if attached_base.is_empty():
            scene = (
                "[gd_scene format=3]\n\n[node name=\"%s\" type=\"%s\"]\n"
                % [autoload_name.c_escape(), str(native_class).c_escape()]
            )
        else:
            scene = (
                "[gd_scene load_steps=2 format=3]\n\n"
                + "[sub_resource type=\"AttachedCompiledScript\" id=\"AttachedScript\"]\n"
                + "source_path = \"%s\"\n\n" % script_path.c_escape()
                + "[node name=\"%s\" type=\"%s\"]\n" % [
                    autoload_name.c_escape(),
                    str(attached_base).c_escape(),
                ]
                + "script = SubResource(\"AttachedScript\")\n"
            )
        _autoload_files[generated_path] = scene
        _autoload_replacements[setting] = generated_path
        _autoload_originals[setting] = original
    return true


func _activate_autoloads() -> void:
    for setting: String in _autoload_replacements:
        var original := str(_autoload_originals[setting])
        var prefix := "*" if original.begins_with("*") else ""
        ProjectSettings.set_setting(setting, prefix + str(_autoload_replacements[setting]))
    for path: String in _autoload_files:
        add_file(path, str(_autoload_files[path]).to_utf8_buffer(), false)


func _restore_autoloads() -> void:
    for setting: String in _autoload_originals:
        ProjectSettings.set_setting(setting, _autoload_originals[setting])
    _autoload_originals.clear()


func _platform_from_features(features: PackedStringArray) -> String:
    var matches: Array[String] = []
    for value in ["macos", "windows", "linux", "android", "ios", "web"]:
        if features.has(value):
            matches.append(value)
    return matches[0] if matches.size() == 1 else ""


func _architecture_from_features(features: PackedStringArray) -> String:
    if _target_platform == "web":
        return "wasm32" if features.has("wasm32") else ""
    if _target_platform == "ios":
        return "arm64" if features.has("arm64") else ""
    if features.has("universal"):
        return "universal"
    var matches: Array[String] = []
    for value in ["arm64", "x86_64"]:
        if features.has(value):
            matches.append(value)
    if matches.size() == 2 and _target_platform == "macos":
        return "universal"
    if matches.size() == 1:
        return matches[0]
    if matches.is_empty() and _compiler != null:
        return _compiler.get_host_architecture()
    return ""


func _web_variant_from_features(features: PackedStringArray) -> String:
    if _target_platform != "web":
        return ""
    var matches: Array[String] = []
    for value in ["threads", "nothreads"]:
        if features.has(value):
            matches.append(value)
    return matches[0] if matches.size() == 1 else ""


func _android_compiler() -> String:
    var ndk_root := str(ProjectSettings.get_setting(ANDROID_NDK_SETTING, ""))
    if ndk_root.is_empty():
        _fail_export(
            "Android NDK is not configured; set '%s' to an installed NDK directory"
            % ANDROID_NDK_SETTING
        )
        return ""
    var host_tag := {
        "macOS": "darwin-x86_64",
        "Windows": "windows-x86_64",
        "Linux": "linux-x86_64",
    }.get(OS.get_name(), "")
    if host_tag.is_empty():
        _fail_export("Android cross-compilation is not supported on host '%s'" % OS.get_name())
        return ""
    var executable := "clang++.exe" if OS.get_name() == "Windows" else "clang++"
    var compiler := ndk_root.path_join(
        "toolchains/llvm/prebuilt/%s/bin/%s" % [host_tag, executable]
    )
    if not FileAccess.file_exists(compiler):
        _fail_export("Android NDK C++ compiler was not found: %s" % compiler)
        return ""
    return compiler


func _web_compiler() -> String:
    var compiler := str(ProjectSettings.get_setting(EMSCRIPTEN_CXX_SETTING, "em++"))
    if compiler.strip_edges().is_empty():
        _fail_export(
            "Emscripten C++ compiler is not configured; set '%s' to em++"
            % EMSCRIPTEN_CXX_SETTING
        )
        return ""
    return compiler


func _ios_compiler() -> String:
    if OS.get_name() != "macOS":
        _fail_export("iOS AOT export requires macOS with a complete Xcode installation")
        return ""
    var output: Array = []
    var status := OS.execute("xcrun", ["--find", "clang++"], output, true)
    if status != 0 or output.is_empty():
        _fail_export(
            "Xcode C++ tools were not found; install Xcode and run xcode-select first"
        )
        return ""
    return "xcrun"


func _native_artifact_exists(path: String) -> bool:
    if FileAccess.file_exists(path):
        return true
    var absolute := ProjectSettings.globalize_path(path)
    return (
        path.get_extension().to_lower() == "xcframework"
        and DirAccess.dir_exists_absolute(absolute)
        and FileAccess.file_exists(path.path_join("Info.plist"))
    )


func _runtime_library_entries(library_path: String, feature := "") -> String:
    var prefix := _target_platform
    if not feature.is_empty():
        prefix += "." + feature
    # Universal 2 is a Mach-O payload property, not a Godot runtime feature.
    # Each process reports its active CPU architecture, and both entries load
    # the same fat dylib.
    if _target_platform == "macos" and _target_architecture == "universal":
        return (
            "%s.arm64 = \"%s\"\n%s.x86_64 = \"%s\"\n"
            % [prefix, library_path, prefix, library_path]
        )
    return "%s.%s = \"%s\"\n" % [prefix, _target_architecture, library_path]


func _export_scan_library_entries(library_path: String, feature := "") -> String:
    var prefix := _target_platform
    if not feature.is_empty():
        prefix += "." + feature
    # Godot's GDExtension export plugin treats `universal` as a special export
    # architecture and expands it to arm64/x86_64. Supplying all three keys
    # makes the same dylib appear more than once depending on HashSet order.
    return "%s.%s = \"%s\"\n" % [prefix, _target_architecture, library_path]


func _shared_object_tags() -> PackedStringArray:
    var tags := PackedStringArray()
    if not _target_platform.is_empty():
        tags.append(_target_platform)
    if (
        not _target_architecture.is_empty()
        and not (_target_platform == "macos" and _target_architecture == "universal")
    ):
        tags.append(_target_architecture)
    if _target_platform == "web" and _target_variant == "threads":
        tags.append("threads")
    return tags


func _collect_scene_replacement_plan(
    scene_root: Node,
    scene_state: SceneState,
    scene_path: String,
    replacement_plan: Array[Dictionary]
) -> bool:
    # Inherited scenes serialize only their differences. Once their root script
    # is replaced by a native class, the inherited root is materialized, so the
    # script properties owned by every base SceneState must be transformed too.
    # Process derived-to-base and let the first occurrence of a node path win;
    # this preserves both script overrides and explicit `script = null` values.
    var handled_script_paths: Dictionary = {}
    for state: SceneState in _scene_state_chain(scene_state):
        for node_index in state.get_node_count():
            var has_script_property := false
            var script_value: Variant = null
            for property_index in state.get_node_property_count(node_index):
                if state.get_node_property_name(node_index, property_index) == &"script":
                    has_script_property = true
                    script_value = state.get_node_property_value(node_index, property_index)
                    break
            if not has_script_property:
                continue

            var node_path := state.get_node_path(node_index)
            var path_key := str(node_path)
            if handled_script_paths.has(path_key):
                continue
            handled_script_paths[path_key] = true
            var node := _state_node(scene_root, node_path)
            if node == null:
                push_error(
                    "GDPP: scene '%s' cannot resolve serialized node '%s'" % [
                        scene_path,
                        node_path,
                    ]
                )
                return false

            if script_value == null:
                # A parent scene may explicitly clear a script inherited from an
                # instanced PackedScene. The child scene is AOT-transformed to a
                # native type, so keeping only `script = null` would no longer
                # remove its behavior. Materialize just this overridden node as
                # its original engine class.
                if state != scene_state or _is_nested_scene_override(node, scene_root):
                    replacement_plan.append({
                        "node": node,
                        "native_class": StringName(node.get_class()),
                        "script_path": "",
                        "stored_properties": _serialized_node_properties(
                            scene_state,
                            node_path
                        ),
                    })
                continue

            if not script_value is Script:
                push_error(
                    "GDPP: scene '%s' has a non-Script value in '%s:script'" % [
                        scene_path,
                        node_path,
                    ]
                )
                return false
            var script_path := _script_path(node)
            if script_path.is_empty():
                script_path = (script_value as Script).resource_path
            if _editor_only_scripts.has(script_path):
                push_error(
                    "GDPP: runtime scene '%s' uses editor-only script '%s'"
                    % [scene_path, script_path]
                )
                return false
            if not _script_classes.has(script_path):
                push_error(
                    "GDPP: scene '%s' uses uncompiled script '%s'" % [scene_path, script_path]
                )
                return false
            if _attached_script_bases.has(script_path):
                var attached_base := StringName(_attached_script_bases[script_path])
                if not node.is_class(attached_base):
                    push_error(
                        (
                            "GDPP: third-party object '%s' for '%s' is not an instance of '%s'"
                        )
                        % [node.get_class(), script_path, attached_base]
                    )
                    return false
                replacement_plan.append({
                    "node": node,
                    "native_class": attached_base,
                    "script_path": script_path,
                    "attached": true,
                    "stored_properties": _serialized_node_properties(
                        scene_state,
                        node_path
                    ),
                })
                continue
            var native_class := StringName(_script_classes[script_path])
            if not ClassDB.is_parent_class(native_class, &"Node"):
                return false
            var concrete_class := node.get_class()
            if not ClassDB.is_parent_class(native_class, concrete_class):
                push_error(
                    (
                        "GDPP: native class '%s' for '%s' cannot preserve scene node type '%s'; "
                        + "declare the script's extends type as '%s' or a derived type"
                    )
                    % [native_class, script_path, concrete_class, concrete_class]
                )
                return false
            replacement_plan.append({
                "node": node,
                "native_class": native_class,
                "script_path": script_path,
                "attached": false,
                "stored_properties": _serialized_node_properties(scene_state, node_path),
            })

    # SceneState stores parents before their children. Replacing from leaves to
    # root keeps every NodePath and owner valid while parent nodes are swapped.
    replacement_plan.reverse()
    return true


func _scene_state_chain(scene_state: SceneState) -> Array[SceneState]:
    var result: Array[SceneState] = []
    var current := scene_state
    while current != null:
        result.append(current)
        # Godot 4.5 exposed SceneState's existing base-state traversal to scripts. On the
        # supported 4.4 baseline every scene resource is still transformed independently, but
        # its local SceneState is the only state the editor API can inspect safely.
        if not current.has_method(&"get_base_scene_state"):
            break
        current = current.call(&"get_base_scene_state") as SceneState
    return result


func _serialized_node_properties(scene_state: SceneState, node_path: NodePath) -> Dictionary:
    var result: Dictionary = {}
    # Derived states override base states. Keep the first serialized value for
    # each name while still materializing inherited properties on the new node.
    for state: SceneState in _scene_state_chain(scene_state):
        for node_index in state.get_node_count():
            if state.get_node_path(node_index) != node_path:
                continue
            for property_index in state.get_node_property_count(node_index):
                var name := str(state.get_node_property_name(node_index, property_index))
                if name == "script" or result.has(name):
                    continue
                result[name] = state.get_node_property_value(node_index, property_index)
            break
    return result


func _is_nested_scene_override(node: Node, scene_root: Node) -> bool:
    var current := node
    while current != null and current != scene_root:
        if not current.scene_file_path.is_empty():
            return true
        current = current.get_parent()
    return false


func _state_node(scene_root: Node, path: NodePath) -> Node:
    if path == NodePath(".") or path.is_empty():
        return scene_root
    return scene_root.get_node_or_null(path)


func _replace_planned_nodes(
    scene_root: Node,
    replacement_plan: Array[Dictionary],
    replacement_nodes: Dictionary,
    resource_replacements: Dictionary,
    resource_changes: Dictionary,
    scene_path: String
) -> Node:
    # Prepare and populate every replacement before touching the source tree.
    # A missing class/property therefore fails the complete scene atomically
    # instead of leaving a partially replaced graph whose scripts are stripped.
    var prepared: Dictionary = {}
    var attached_scripts: Dictionary = {}
    var copied_properties := 0
    for plan: Dictionary in replacement_plan:
        var node: Node = plan.node
        if bool(plan.get("attached", false)):
            var compiled_script := _make_attached_script(str(plan.script_path))
            if compiled_script == null:
                _free_prepared_replacements(prepared)
                return null
            attached_scripts[node.get_instance_id()] = compiled_script
            continue
        var native_class := StringName(plan.native_class)
        var replacement := ClassDB.instantiate(native_class) as Node
        if replacement == null:
            push_error("GDPP: cannot instantiate native scene class '%s'" % native_class)
            _free_prepared_replacements(prepared)
            return null
        var copied := _copy_storage_properties(
            node,
            replacement,
            plan.get("stored_properties", {}),
            resource_replacements,
            resource_changes,
            scene_path
        )
        if copied < 0:
            replacement.free()
            _free_prepared_replacements(prepared)
            return null
        copied_properties += copied
        replacement.name = node.name
        replacement.unique_name_in_owner = node.unique_name_in_owner
        for group: StringName in node.get_groups():
            replacement.add_to_group(group, true)
        prepared[node.get_instance_id()] = replacement

    var result := scene_root
    for plan: Dictionary in replacement_plan:
        var node: Node = plan.node
        if bool(plan.get("attached", false)):
            var attached_copied := _install_attached_script(
                node,
                attached_scripts[node.get_instance_id()],
                plan.get("stored_properties", {}),
                resource_replacements,
                resource_changes,
                scene_path
            )
            if attached_copied < 0:
                _free_prepared_replacements(prepared)
                return null
            copied_properties += attached_copied
            continue
        var replaces_root := node == result
        var replacement: Node = prepared[node.get_instance_id()]
        _replace_node(node, replacement, replacement_nodes)
        if replaces_root:
            result = replacement
    replacement_nodes["__gdpp_copied_properties"] = copied_properties
    return result


func _transform_serialized_scene_resources(
    scene_root: Node,
    scene_state: SceneState,
    scene_path: String,
    replacements: Dictionary,
    changes: Dictionary
) -> void:
    # Script-backed built-in Resources may be stored on engine nodes that do
    # not themselves have a script. Walk exactly the properties serialized by
    # the SceneState chain, so optional/non-storage getters are never observed.
    var handled_properties: Dictionary = {}
    for state: SceneState in _scene_state_chain(scene_state):
        for node_index in state.get_node_count():
            var node_path := state.get_node_path(node_index)
            var node := _state_node(scene_root, node_path)
            if node == null:
                continue
            for property_index in state.get_node_property_count(node_index):
                var name := str(state.get_node_property_name(node_index, property_index))
                if name == "script":
                    continue
                var property_key := "%s\n%s" % [node_path, name]
                if handled_properties.has(property_key):
                    continue
                handled_properties[property_key] = true
                var original: Variant = node.get(name)
                if not (original is Resource or original is Array or original is Dictionary):
                    continue
                var change_count_before := int(changes.get("count", 0))
                var transformed := _sanitize_storage_value(
                    original,
                    replacements,
                    changes,
                    scene_path
                )
                if int(changes.get("count", 0)) <= change_count_before:
                    continue
                if name.begins_with("metadata/"):
                    node.set_meta(name.trim_prefix("metadata/"), transformed)
                else:
                    node.set(name, transformed)


func _replace_node(
    node: Node,
    replacement: Node,
    replacement_nodes: Dictionary
) -> void:
    # ClassDB instances start with an engine-generated name. Node.replace_by() does not promise to
    # transfer the old name, but NodePath references are serialized against it.
    var old_id := node.get_instance_id()
    var original_owner := node.owner
    var parent := node.get_parent()
    if parent != null:
        node.replace_by(replacement, true)
        replacement.owner = original_owner
        _replace_owner(replacement, node, replacement)
    else:
        for child in node.get_children():
            var owned_by_root := child.owner == node
            if owned_by_root:
                child.owner = null
            node.remove_child(child)
            replacement.add_child(child)
            if owned_by_root:
                child.owner = replacement
        _replace_owner(replacement, node, replacement)
    replacement_nodes[old_id] = replacement
    node.free()


func _free_prepared_replacements(prepared: Dictionary) -> void:
    for replacement: Node in prepared.values():
        replacement.free()


func _replace_owner(node: Node, old_owner: Node, new_owner: Node) -> void:
    for child in node.get_children():
        if child.owner == old_owner:
            child.owner = new_owner
        _replace_owner(child, old_owner, new_owner)


func _make_attached_script(script_path: String) -> Script:
    var instance := ClassDB.instantiate(&"AttachedCompiledScript")
    if not instance is Script:
        if instance != null:
            instance.free()
        push_error("GDPP: cannot instantiate the attached script runtime")
        return null
    var script := instance as Script
    script.set("source_path", script_path)
    if script.get_instance_base_type().is_empty() or str(script.get("source_path")) != script_path:
        push_error("GDPP: compiled script descriptor for '%s' is unavailable" % script_path)
        return null
    return script


func _storage_property_values(
    source: Object,
    serialized_properties: Variant = null
) -> Dictionary:
    if serialized_properties is Dictionary:
        return (serialized_properties as Dictionary).duplicate()
    var values: Dictionary = {}
    for property: Dictionary in source.get_property_list():
        var property_name := str(property.get("name", ""))
        var usage := int(property.get("usage", 0))
        if property_name == "script" or (usage & PROPERTY_USAGE_STORAGE) == 0:
            continue
        values[property_name] = source.get(property_name)
    return values


func _restore_storage_properties(
    source_class: StringName,
    destination: Object,
    source_properties: Dictionary,
    replacements: Dictionary,
    changes: Dictionary,
    context_path: String
) -> int:
    var destination_properties: Dictionary = {}
    for property: Dictionary in destination.get_property_list():
        destination_properties[str(property.get("name", ""))] = true
    var copied := 0
    for name: String in source_properties:
        # Godot exposes Object metadata as dynamic `metadata/<key>` storage
        # properties. A fresh native instance does not list those properties
        # until the metadata is created, so treating them as a missing native
        # field would reject otherwise valid scenes. Preserve both editor and
        # runtime metadata through Object's metadata API.
        if name.begins_with("metadata/"):
            var metadata_name := name.trim_prefix("metadata/")
            destination.set_meta(
                metadata_name,
                _sanitize_storage_value(
                    source_properties[name],
                    replacements,
                    changes,
                    context_path
                )
            )
            copied += 1
            continue
        if not destination_properties.has(name):
            push_error(
                "GDPP: native class '%s' is missing stored property '%s' from '%s'" % [
                    destination.get_class(),
                    name,
                    source_class,
                ]
            )
            return -1
        destination.set(
            name,
            _sanitize_storage_value(
                source_properties[name],
                replacements,
                changes,
                context_path
            )
        )
        copied += 1
    return copied


func _install_attached_script(
    object: Object,
    script: Script,
    serialized_properties: Variant = null,
    resource_replacements: Variant = null,
    resource_changes: Variant = null,
    context_path: String = ""
) -> int:
    var replacements: Dictionary = (
        resource_replacements if resource_replacements is Dictionary else {}
    )
    var changes: Dictionary = resource_changes if resource_changes is Dictionary else {"count": 0}
    var source_class := StringName(object.get_class())
    var source_properties := _storage_property_values(object, serialized_properties)
    object.set_script(script)
    if object.get_script() != script:
        push_error(
            "GDPP: third-party object '%s' rejected compiled script '%s'" % [
                source_class,
                str(script.get("source_path")),
            ]
        )
        return -1
    return _restore_storage_properties(
        source_class,
        object,
        source_properties,
        replacements,
        changes,
        context_path
    )


func _attach_compiled_script(
    object: Object,
    script_path: String,
    serialized_properties: Variant = null,
    resource_replacements: Variant = null,
    resource_changes: Variant = null,
    context_path: String = ""
) -> int:
    var script := _make_attached_script(script_path)
    if script == null:
        return -1
    return _install_attached_script(
        object,
        script,
        serialized_properties,
        resource_replacements,
        resource_changes,
        context_path
    )


func _copy_storage_properties(
    source: Object,
    destination: Object,
    serialized_properties: Variant = null,
    resource_replacements: Variant = null,
    resource_changes: Variant = null,
    context_path: String = ""
) -> int:
    var replacements: Dictionary = (
        resource_replacements if resource_replacements is Dictionary else {}
    )
    var changes: Dictionary = resource_changes if resource_changes is Dictionary else {"count": 0}
    return _restore_storage_properties(
        StringName(source.get_class()),
        destination,
        _storage_property_values(source, serialized_properties),
        replacements,
        changes,
        context_path
    )


func _sanitize_storage_value(
    value: Variant,
    resource_replacements: Dictionary = {},
    resource_changes: Dictionary = {"count": 0},
    context_path: String = ""
) -> Variant:
    # Godot's typed Array/Dictionary metadata can name the original GDScript
    # resource. The exporter then rejects the native replacement even though it
    # represents the same project class. Rebuild containers without script
    # constraints while recursively preserving their values and insertion order.
    if value is Array:
        var result: Array = []
        result.resize(value.size())
        for index in value.size():
            result[index] = _sanitize_storage_value(
                value[index],
                resource_replacements,
                resource_changes,
                context_path
            )
        return result
    if value is Dictionary:
        var result: Dictionary = {}
        for key: Variant in value:
            result[
                _sanitize_storage_value(
                    key,
                    resource_replacements,
                    resource_changes,
                    context_path
                )
            ] = _sanitize_storage_value(
                value[key],
                resource_replacements,
                resource_changes,
                context_path
            )
        return result
    if value is Resource:
        return _transform_resource_graph(
            value,
            context_path,
            resource_replacements,
            resource_changes,
            false
        )
    return value


func _transform_resource_graph(
    resource: Resource,
    context_path: String,
    replacements: Dictionary,
    changes: Dictionary,
    is_root: bool
) -> Resource:
    if resource == null:
        return null
    var instance_id := resource.get_instance_id()
    if replacements.has(instance_id):
        return replacements[instance_id]

    # External Resources are exported through their own path and receive an
    # explicit remap in _export_file(). Only the current root and built-in
    # subresources may be mutated here; this preserves sharing and relative
    # dependency semantics across scenes.
    var resource_path := resource.resource_path
    if not is_root and not resource_path.is_empty() and not resource_path.contains("::"):
        return resource

    if resource is PackedScene:
        # PackedScene-valued properties can contain their own serialized node
        # graph (for example a projectile template embedded in a tower Resource).
        # Traversing `_bundled` as an ordinary Dictionary would retain Script
        # variants; instantiate and repack it so node types are native too.
        replacements[instance_id] = resource
        var nested_root := (resource as PackedScene).instantiate(
            PackedScene.GEN_EDIT_STATE_INSTANCE
        )
        if nested_root == null:
            replacements.erase(instance_id)
            _fail_export("embedded PackedScene in '%s' cannot be instantiated" % context_path)
            return null
        var transformed_root := _transform_scene_with_state(
            nested_root,
            (resource as PackedScene).get_state(),
            "%s::PackedScene" % context_path,
            replacements,
            changes
        )
        if transformed_root == null:
            if is_instance_valid(nested_root):
                nested_root.free()
            if _strict_failure_injected:
                replacements.erase(instance_id)
                return null
            return resource
        var transformed_scene := PackedScene.new()
        var pack_error := transformed_scene.pack(transformed_root)
        transformed_root.free()
        if pack_error != OK:
            replacements.erase(instance_id)
            _fail_export(
                "embedded PackedScene in '%s' cannot be packed (error %d)"
                % [context_path, pack_error]
            )
            return null
        transformed_scene.resource_name = resource.resource_name
        transformed_scene.resource_local_to_scene = resource.resource_local_to_scene
        for metadata_name: StringName in resource.get_meta_list():
            transformed_scene.set_meta(metadata_name, resource.get_meta(metadata_name))
        replacements[instance_id] = transformed_scene
        changes.count = int(changes.get("count", 0)) + 1
        return transformed_scene

    var script_path := _script_path(resource)
    if not script_path.is_empty():
        if _editor_only_scripts.has(script_path):
            _fail_export(
                "runtime resource graph '%s' uses editor-only script '%s'"
                % [context_path, script_path]
            )
            return null
        if not _script_classes.has(script_path):
            _fail_export(
                "resource graph '%s' uses an uncompiled script '%s'"
                % [context_path, script_path]
            )
            return null
        if _attached_script_bases.has(script_path):
            # Install the identity mapping before restoring fields so self-references and
            # cyclic built-in Resource graphs retain their exact topology.
            replacements[instance_id] = resource
            var copied_attached := _attach_compiled_script(
                resource,
                script_path,
                null,
                replacements,
                changes,
                context_path
            )
            if copied_attached < 0:
                replacements.erase(instance_id)
                _fail_export(
                    "resource graph '%s' cannot attach compiled script '%s'" % [
                        context_path,
                        script_path,
                    ]
                )
                return null
            changes.count = int(changes.get("count", 0)) + 1
            _metrics_mutex.lock()
            _customized_resource_count += 1
            _copied_property_count += copied_attached
            _metrics_mutex.unlock()
            return resource
        var replacement := ClassDB.instantiate(StringName(_script_classes[script_path]))
        if not replacement is Resource:
            if replacement != null:
                replacement.free()
            _fail_export("native class for '%s' is not a Resource" % script_path)
            return null
        # Install the identity mapping before copying so self-references and
        # cyclic built-in Resource graphs terminate and retain their topology.
        replacements[instance_id] = replacement
        var copied := _copy_storage_properties(
            resource,
            replacement,
            null,
            replacements,
            changes,
            context_path
        )
        if copied < 0:
            replacements.erase(instance_id)
            replacement.free()
            _fail_export(
                "resource graph '%s' cannot preserve all stored properties" % context_path
            )
            return null
        changes.count = int(changes.get("count", 0)) + 1
        _metrics_mutex.lock()
        _customized_resource_count += 1
        _copied_property_count += copied
        _metrics_mutex.unlock()
        return replacement

    replacements[instance_id] = resource
    # Traverse storage properties only. Godot's 4.4/4.5 global resource pass
    # reads every Object property, including optional getters such as
    # GPUParticles3D.draw_pass_2..4; restricting traversal to serialized state
    # avoids observable reads that are invalid for the current object shape.
    for property: Dictionary in resource.get_property_list():
        var name := str(property.get("name", ""))
        var usage := int(property.get("usage", 0))
        if name == "script" or (usage & PROPERTY_USAGE_STORAGE) == 0:
            continue
        var original: Variant = resource.get(name)
        if not (original is Resource or original is Array or original is Dictionary):
            continue
        var change_count_before := int(changes.get("count", 0))
        var transformed := _sanitize_storage_value(
            original,
            replacements,
            changes,
            context_path
        )
        if int(changes.get("count", 0)) > change_count_before:
            resource.set(name, transformed)
    return resource


func _script_path(object: Object) -> String:
    var script: Script = object.get_script()
    return "" if script == null else script.resource_path


func _snapshot_connections(root: Node, scene_state: SceneState) -> Array[Dictionary]:
    var result: Array[Dictionary] = []
    # SceneState lists only the connections serialized by this scene. Reading
    # it avoids copying persistent connections owned by nested PackedScenes
    # into their parent, which would make signals fire more than once.
    for state: SceneState in _scene_state_chain(scene_state):
        for connection_index in state.get_connection_count():
            var source := _state_node(
                root,
                state.get_connection_source(connection_index)
            )
            var target := _state_node(
                root,
                state.get_connection_target(connection_index)
            )
            if source == null or target == null:
                continue
            var signal_name := state.get_connection_signal(connection_index)
            var callable := Callable(
                target,
                state.get_connection_method(connection_index)
            ).bindv(state.get_connection_binds(connection_index))
            var unbound := state.get_connection_unbinds(connection_index)
            if unbound > 0:
                callable = callable.unbind(unbound)
            var signal_value := Signal(source, signal_name)
            # The same connection can appear in more than one state in an
            # inheritance chain. Disconnecting it once naturally deduplicates
            # the snapshot without inventing a separate key format.
            if not signal_value.is_connected(callable):
                continue
            result.append({
                "source_id": source.get_instance_id(),
                "source": source,
                "signal": signal_name,
                "target_id": target.get_instance_id(),
                "target": target,
                "method": state.get_connection_method(connection_index),
                "bound": state.get_connection_binds(connection_index),
                "unbound": unbound,
                "flags": state.get_connection_flags(connection_index),
            })
            signal_value.disconnect(callable)
    return result


func _restore_connections(
    connection_snapshots: Array[Dictionary],
    replacement_nodes: Dictionary
) -> void:
    for connection: Dictionary in connection_snapshots:
        var source: Object = replacement_nodes.get(
            connection.source_id,
            connection.source
        )
        var target: Object = replacement_nodes.get(
            connection.target_id,
            connection.target
        )
        if source == null or target == null:
            continue
        var callable := Callable(target, connection.method).bindv(connection.bound)
        if int(connection.unbound) > 0:
            callable = callable.unbind(int(connection.unbound))
        var signal_value := Signal(source, connection.signal)
        if not signal_value.is_connected(callable):
            signal_value.connect(callable, int(connection.flags))


func _fail_export(message: String) -> void:
    _ready = false
    push_error("GDPP export: %s" % message)
    var platform := get_export_platform()
    if platform != null:
        var allow_fallback: bool = get_option(ALLOW_SOURCE_FALLBACK_OPTION) == true
        var fail_closed: bool = get_option(STRIP_OPTION) == true and not allow_fallback
        platform.add_message(
            (
                EditorExportPlatform.EXPORT_MESSAGE_ERROR
                if fail_closed
                else EditorExportPlatform.EXPORT_MESSAGE_WARNING
            ),
            "GDPP AOT",
            (
                "%s; binary-only export is blocked to prevent source disclosure"
                if fail_closed
                else "%s; this export keeps the original GDScript sources"
            ) % message
        )
        if fail_closed:
            _inject_strict_export_failure()


func _inject_strict_export_failure() -> void:
    if _strict_failure_injected:
        return
    _strict_failure_injected = true
    var extension := {
        "macos": "dylib",
        "linux": "so",
        "windows": "dll",
        "android": "so",
        "ios": "xcframework",
        "web": "wasm",
    }.get(_target_platform, "invalid")
    var sentinel := "res://addons/gdpp/build/__gdpp_aot_export_failed__.%s" % extension
    var tags := _shared_object_tags()
    # EditorExportPlugin has no public abort callback in Godot 4.4–4.7.
    # A deliberately absent shared object turns a fail-closed AOT error into
    # the platform export's own ERR_FILE_NOT_FOUND instead of a successful
    # package that silently contains customer source code.
    add_shared_object(sentinel, tags, "")


func _prepare_extension_registry(include_project_extension := true) -> bool:
    _include_project_extension = include_project_extension
    var retained: PackedStringArray = []
    if FileAccess.file_exists(EXTENSION_REGISTRY):
        for line in FileAccess.get_file_as_string(EXTENSION_REGISTRY).split("\n", false):
            var value := line.strip_edges()
            if value.is_empty():
                continue
            if value == COMPILER_DESCRIPTOR:
                continue
            # Prefer providers before the generated project extension for engines that preserve
            # this file's order. Godot may regenerate the registry from a HashSet, so the attached
            # runtime is also required to initialize correctly in the opposite order.
            if value == LEGACY_RUNTIME_DESCRIPTOR:
                continue
            if value.begins_with(OUTPUT_DIRECTORY + "/"):
                continue
            if not retained.has(value):
                retained.append(value)
    if not _prepare_provider_scan_descriptors(retained):
        return false
    if include_project_extension:
        retained.append(COMPILER_DESCRIPTOR)
    _export_extension_registry = "" if retained.is_empty() else "\n".join(retained) + "\n"
    # `.godot/extension_list.cfg` is generated metadata and is not guaranteed to pass through
    # `_export_file()`. Add the sanitized registry explicitly; otherwise a virtual runtime
    # descriptor can be present while independent PCK consumers have no load-order contract.
    if not _export_extension_registry.is_empty():
        add_file(EXTENSION_REGISTRY, _export_extension_registry.to_utf8_buffer(), false)
    var original := ""
    if FileAccess.file_exists(EXTENSION_REGISTRY):
        original = FileAccess.get_file_as_string(EXTENSION_REGISTRY)
    if original != _export_extension_registry:
        # Godot 4.5 regenerates extension_list.cfg after _export_file() callbacks.
        # Temporarily replace the editor registry itself so its forced export pass
        # cannot reintroduce the compiler-only GDExtension into the game package.
        if not _write_text_file(EXTENSION_REGISTRY_BACKUP, original):
            _fail_export("cannot create the extension registry transaction backup")
            return false
        if not _write_text_file(EXTENSION_REGISTRY, _export_extension_registry):
            DirAccess.remove_absolute(ProjectSettings.globalize_path(EXTENSION_REGISTRY_BACKUP))
            _fail_export("cannot prepare the runtime extension registry")
            return false
        _extension_registry_original = original
        _extension_registry_modified = true
    if not _prepare_compiler_descriptor():
        _restore_export_transaction()
        return false
    return true


func _clear_godot_export_cache() -> bool:
    var absolute := ProjectSettings.globalize_path(GODOT_EXPORT_CACHE_DIRECTORY)
    if not DirAccess.dir_exists_absolute(absolute):
        return true
    return _remove_directory_contents(absolute)


func _remove_directory_contents(absolute: String) -> bool:
    var directory := DirAccess.open(absolute)
    if directory == null:
        return false
    var success := true
    directory.list_dir_begin()
    while true:
        var name := directory.get_next()
        if name.is_empty():
            break
        if name in [".", ".."]:
            continue
        var child := absolute.path_join(name)
        if directory.current_is_dir():
            if not _remove_directory_contents(child):
                success = false
            elif DirAccess.remove_absolute(child) != OK:
                success = false
        elif DirAccess.remove_absolute(child) != OK:
            success = false
    directory.list_dir_end()
    return success


func _prepare_compiler_descriptor() -> bool:
    if _export_scan_descriptor.is_empty() or not FileAccess.file_exists(COMPILER_DESCRIPTOR):
        return true
    var original := FileAccess.get_file_as_string(COMPILER_DESCRIPTOR)
    if original == _export_scan_descriptor:
        return true
    if not _write_text_file(COMPILER_DESCRIPTOR_BACKUP, original):
        _fail_export("cannot create the compiler descriptor transaction backup")
        return false
    if not _write_text_file(COMPILER_DESCRIPTOR, _export_scan_descriptor):
        DirAccess.remove_absolute(ProjectSettings.globalize_path(COMPILER_DESCRIPTOR_BACKUP))
        _fail_export("cannot prepare the project library scan descriptor")
        return false
    _compiler_descriptor_original = original
    _compiler_descriptor_modified = true
    return true


func _prepare_provider_scan_descriptors(descriptors: PackedStringArray) -> bool:
    if _target_platform != "macos" or _target_architecture != "universal":
        return true

    var originals: Dictionary = {}
    var rewritten: Dictionary = {}
    for path: String in descriptors:
        if path.get_extension().to_lower() != "gdextension":
            continue
        if not FileAccess.file_exists(path):
            _fail_export("provider extension descriptor does not exist: %s" % path)
            return false
        var config := ConfigFile.new()
        if config.load(path) != OK:
            _fail_export("cannot parse provider extension descriptor: %s" % path)
            return false
        if not config.has_section("libraries"):
            continue

        var pairs: Dictionary = {}
        var library_keys := config.get_section_keys("libraries")
        for key: String in library_keys:
            var parts := key.split(".", false)
            if not parts.has("macos") or not parts.has("arm64"):
                continue
            var architecture_index := parts.find("arm64")
            var sibling_parts := parts.duplicate()
            sibling_parts[architecture_index] = "x86_64"
            var sibling_key := ".".join(sibling_parts)
            if not config.has_section_key("libraries", sibling_key):
                continue
            var arm_library := str(config.get_value("libraries", key, ""))
            var x86_library := str(config.get_value("libraries", sibling_key, ""))
            if arm_library.is_empty() or arm_library != x86_library:
                continue
            var universal_parts := parts.duplicate()
            universal_parts[architecture_index] = "universal"
            var universal_key := ".".join(universal_parts)
            if (
                config.has_section_key("libraries", universal_key)
                and str(config.get_value("libraries", universal_key, "")) != arm_library
            ):
                _fail_export(
                    (
                        "provider extension '%s' has conflicting '%s' and dual-architecture "
                        + "library entries"
                    )
                    % [path, universal_key]
                )
                return false
            if not _is_universal_macos_library(arm_library):
                _fail_export(
                    (
                        "provider extension '%s' maps arm64 and x86_64 to '%s', but the "
                        + "library is not a verifiable Universal 2 Mach-O"
                    )
                    % [path, arm_library]
                )
                return false
            pairs[universal_key] = {
                "arm": key,
                "x86": sibling_key,
                "library": arm_library,
            }

        if pairs.is_empty():
            continue
        var original := FileAccess.get_file_as_string(path)
        if original.is_empty():
            _fail_export("cannot read provider extension descriptor: %s" % path)
            return false
        for universal_key: String in pairs:
            var pair: Dictionary = pairs[universal_key]
            config.erase_section_key("libraries", str(pair.arm))
            config.erase_section_key("libraries", str(pair.x86))
            config.set_value("libraries", universal_key, str(pair.library))
        originals[path] = original
        rewritten[path] = config.encode_to_text()

    if rewritten.is_empty():
        return true
    if not _write_text_file(PROVIDER_DESCRIPTORS_BACKUP, JSON.stringify(originals)):
        _fail_export("cannot create the provider descriptor transaction backup")
        return false
    _provider_descriptor_originals = originals
    for path: String in rewritten:
        if not _write_text_file(path, str(rewritten[path])):
            _restore_provider_descriptors()
            _fail_export("cannot prepare provider extension scan descriptor: %s" % path)
            return false
    return true


func _is_universal_macos_library(resource_path: String) -> bool:
    var binary_path := ProjectSettings.globalize_path(resource_path)
    if DirAccess.dir_exists_absolute(binary_path) and resource_path.ends_with(".framework"):
        binary_path = binary_path.path_join(resource_path.get_file().get_basename())
    if not FileAccess.file_exists(binary_path):
        return false
    var output: Array = []
    if OS.execute("/usr/bin/lipo", ["-archs", binary_path], output, true) != 0:
        return false
    var architectures := PackedStringArray()
    for line: Variant in output:
        architectures.append_array(str(line).strip_edges().split(" ", false))
    return architectures.has("arm64") and architectures.has("x86_64")


func _restore_export_transaction() -> void:
    _restore_provider_descriptors()
    _restore_compiler_descriptor()
    _restore_extension_registry()


func _restore_provider_descriptors() -> void:
    if (
        _provider_descriptor_originals.is_empty()
        and FileAccess.file_exists(PROVIDER_DESCRIPTORS_BACKUP)
    ):
        var recovered: Variant = JSON.parse_string(
            FileAccess.get_file_as_string(PROVIDER_DESCRIPTORS_BACKUP)
        )
        if not recovered is Dictionary:
            push_error("GDPP export: cannot parse the provider descriptor transaction backup")
            return
        _provider_descriptor_originals = recovered
    if _provider_descriptor_originals.is_empty():
        return
    for path: String in _provider_descriptor_originals:
        if not _write_text_file(path, str(_provider_descriptor_originals[path])):
            push_error("GDPP export: cannot restore provider descriptor '%s'" % path)
            return
    DirAccess.remove_absolute(ProjectSettings.globalize_path(PROVIDER_DESCRIPTORS_BACKUP))
    _provider_descriptor_originals.clear()


func _restore_compiler_descriptor() -> void:
    if not _compiler_descriptor_modified and FileAccess.file_exists(COMPILER_DESCRIPTOR_BACKUP):
        _compiler_descriptor_original = FileAccess.get_file_as_string(COMPILER_DESCRIPTOR_BACKUP)
        _compiler_descriptor_modified = true
    if not _compiler_descriptor_modified:
        return
    if not _write_text_file(COMPILER_DESCRIPTOR, _compiler_descriptor_original):
        push_error("GDPP export: cannot restore the editor compiler descriptor")
        return
    DirAccess.remove_absolute(ProjectSettings.globalize_path(COMPILER_DESCRIPTOR_BACKUP))
    _compiler_descriptor_original = ""
    _compiler_descriptor_modified = false


func _restore_extension_registry() -> void:
    if not _extension_registry_modified and FileAccess.file_exists(EXTENSION_REGISTRY_BACKUP):
        _extension_registry_original = FileAccess.get_file_as_string(EXTENSION_REGISTRY_BACKUP)
        _extension_registry_modified = true
    if not _extension_registry_modified:
        return
    if not _write_text_file(EXTENSION_REGISTRY, _extension_registry_original):
        push_error("GDPP export: cannot restore the editor extension registry")
        return
    DirAccess.remove_absolute(ProjectSettings.globalize_path(EXTENSION_REGISTRY_BACKUP))
    _extension_registry_original = ""
    _extension_registry_modified = false


func _write_text_file(path: String, contents: String) -> bool:
    var file := FileAccess.open(path, FileAccess.WRITE)
    if file == null:
        return false
    file.store_string(contents)
    return file.get_error() == OK


func _remove_successful_export_fallback() -> void:
    if _export_output_path.is_empty() or (not _ready and _include_project_extension):
        return
    var extension := {
        "macos": "dylib",
        "linux": "so",
        "windows": "dll",
        "android": "so",
    }.get(_target_platform, "")
    if extension.is_empty():
        return
    var prefix := "" if _target_platform == "windows" else "lib"
    var filename := "%sgdpp_fallback.%s.%s.%s" % [
        prefix,
        _target_platform,
        _target_architecture,
        extension,
    ]
    var exported_path := _export_output_path.get_base_dir().path_join(filename)
    if FileAccess.file_exists(exported_path):
        DirAccess.remove_absolute(exported_path)


func _activate_fallback_descriptors(is_debug: bool) -> void:
    if _target_platform.is_empty() or _target_architecture.is_empty():
        return
    var extension := {
        "macos": "dylib",
        "linux": "so",
        "windows": "dll",
    }.get(_target_platform, "")
    if extension.is_empty():
        _runtime_descriptor = ""
        _export_scan_descriptor = ""
        return
    var prefix := "" if _target_platform == "windows" else "lib"
    var filename := "%sgdpp_fallback.%s.%s.%s" % [
        prefix,
        _target_platform,
        _target_architecture,
        extension,
    ]
    var library_path := "res://addons/gdpp/binary/%s" % filename
    var feature := "debug" if is_debug else "release"
    var descriptor := (
        "[configuration]\n\n"
        + "entry_symbol = \"gdpp_export_fallback_library_init\"\n"
        + "compatibility_minimum = \"4.4\"\n"
        + "reloadable = false\n\n"
        + "[libraries]\n\n"
        + _runtime_library_entries(library_path, feature)
    )
    _runtime_descriptor = descriptor
    _export_scan_descriptor = (
        "[configuration]\n\n"
        + "entry_symbol = \"gdpp_export_fallback_library_init\"\n"
        + "compatibility_minimum = \"4.4\"\n"
        + "reloadable = false\n\n"
        + "[libraries]\n\n"
        + _export_scan_library_entries(library_path, feature)
    )


func _prepare_script_class_cache() -> bool:
    if not FileAccess.file_exists(SCRIPT_CLASS_CACHE):
        _export_script_class_cache = ""
        return true
    var config := ConfigFile.new()
    if config.load(SCRIPT_CLASS_CACHE) != OK:
        _fail_export("cannot read Godot's global script class cache")
        return false
    var entries: Array = config.get_value("", "list", [])
    var retained: Array = []
    for entry: Dictionary in entries:
        if not _compiled_scripts.has(str(entry.get("path", ""))):
            retained.append(entry)
    config.set_value("", "list", retained)
    _export_script_class_cache = config.encode_to_text()
    return true


func _record_scene_metrics(replaced_nodes: int, copied_properties: int) -> void:
    _metrics_mutex.lock()
    _customized_scene_count += 1
    _replaced_node_count += replaced_nodes
    _copied_property_count += copied_properties
    _metrics_mutex.unlock()


func _print_export_summary() -> void:
    if not _ready:
        return
    _metrics_mutex.lock()
    var scene_count := _customized_scene_count
    var node_count := _replaced_node_count
    var resource_count := _customized_resource_count
    var property_count := _copied_property_count
    _metrics_mutex.unlock()
    var cache_state := "reused_or_empty" if scene_count == 0 and resource_count == 0 else "rebuilt"
    print(
        "GDPP_AOT_SUMMARY scenes=%d nodes=%d resources=%d properties=%d cache=%s" % [
            scene_count,
            node_count,
            resource_count,
            property_count,
            cache_state,
        ]
    )


func _reset_export_state() -> void:
    _ready = false
    _script_classes.clear()
    _attached_script_bases.clear()
    _compiled_scripts.clear()
    _abstract_scripts.clear()
    _editor_only_scripts.clear()
    _runtime_descriptor = ""
    _export_scan_descriptor = ""
    _output_library = ""
    _build_id = ""
    _target_platform = ""
    _target_architecture = ""
    _target_variant = ""
    _build_profile = ""
    _export_extension_registry = ""
    _export_script_class_cache = ""
    _provider_descriptor_originals.clear()
    _include_project_extension = false
    _has_resource_scripts = false
    _export_output_path = ""
    _autoload_files.clear()
    _autoload_replacements.clear()
    _autoload_originals.clear()
    _metrics_mutex.lock()
    _customized_scene_count = 0
    _replaced_node_count = 0
    _customized_resource_count = 0
    _copied_property_count = 0
    _strict_failure_injected = false
    _metrics_mutex.unlock()
