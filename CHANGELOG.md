## 1.7.2

- Completed lossless third-party GDExtension reflection for Variant, typed Array/Dictionary, Signal, object classes, engine-typed Dictionary contracts, and encoded container metadata, preserving provider APIs without source changes.
- Reworked internal-class overrides around the emitted C++ ABI: compatible overrides now use the native virtual slot, incompatible GDScript contracts receive private implementation symbols, and Variant receivers retain dynamic dispatch.
- Propagated internal method contracts through receiver, `super`, default-argument, variadic, and coroutine calls so inherited behavior no longer falls back to name-only or semantic-type guesses.
- Canonicalized nested enum identities across generated declarations, parameters, returns, containers, and cross-script references, eliminating invalid C++ type names in large multi-script builds.
- Made attached-script descriptor replacement compare complete property, method, signal, constant, default-value, metadata, and RPC identities; hardened Callable copy/move assignment against self-aliasing.
- Advanced the packaged SDK to schema 8, requiring the MSVC frontend, a declared 19.x toolset, static CRT compatibility, and matching manifest metadata before Windows commands are created.
- Centralized the delivered target matrix and now reject unavailable Windows arm64, Linux arm64, and Android x86_64 exports before project generation; generated descriptors no longer advertise missing desktop ARM64 libraries.
- Expanded independent-provider, dual-load-order, strict generated-C++ and official Godot 4.6.x regression gates, including a large multi-script compatibility corpus.
- Kept the zero-source-change workflow export-only: ordinary editing and runtime continue to use the project's existing `.gd` files; extended or new `.gdpp` syntax is outside this release.

## 1.7.1

- Completed typed variadic functions, constructors, methods, lambdas, reflection metadata, call thunks, cache fingerprints, and attached-script dispatch across Godot 4.4–4.7 while keeping the grammar owned by GDPP rather than the host GDScript parser.
- Completed cross-script preload namespaces for root and nested classes, enums, constants, static fields and functions, typed resource references, casts, type tests, inheritance, and canonical inspector enum identities.
- Hardened cross-version native object lowering for RefCounted values, raw Object pointers, singleton wrappers, property getter return types, explicitly typed null branches, dynamic calls, and coroutine return ABIs.
- Made runtime/export discovery resolve script and scene Autoload UIDs, rewrite stripped Autoloads transactionally, reject editor-only runtime graphs, and isolate editor-only generated registrations.
- Eliminated native resource retention and self-assignment corruption by routing Dictionary, typed containers, String, StringName, NodePath, Array, PackedArray, Callable, and Signal storage through guarded assignment; attached-script descriptors now preserve the same rule.
- Centralized nested CMake toolchain propagation for compiler, toolchain file, sysroot, target triple, Apple deployment settings, MSVC CRT, RC/MT tools, generator platform, and toolset; independently built provider extensions now link the packaged SDK without CRT drift.
- Advanced the packaged SDK to schema 7 and runtime ABI 8, with fail-closed C++17, exception model, static MSVC CRT, Android `c++_shared`, source-integrity, platform, architecture, profile, and runtime validation.
- Corrected the Godot 4.4 compatibility gate to admit only the exact built-in parser diagnostics caused by newer GDPP-owned syntax while continuing to reject every unrelated import, export, runtime, and PCK diagnostic.
- Added real Godot 4.4 native stress coverage for reference-backed value self-assignment and repeated dynamic, typed, and static Dictionary release, plus independent provider SDK builds on macOS, Linux, and Windows.
- Revalidated the binary delivery path on official Godot 4.6.1, including AOT Autoloads, independent third-party GDExtension startup, exported runtime execution, and zero source/compiler/SDK leakage.

## 1.7.0

- Added zero-source-change AOT support for GDScript classes that extend types owned by an independent third-party GDExtension, without rebuilding, relinking, or modifying the provider plugin.
- Introduced an attached AOT backend built on `ScriptLanguageExtension`, `ScriptExtension`, and `ScriptInstance`: the provider continues to own the native Object while generated C++17 behavior supplies script fields, methods, properties, signals, notifications, and RPC metadata.
- Preserved script inheritance above an attached native root, including field initialization, `_init`, method/property dispatch, virtual callbacks, signal behavior, and inherited or overridden RPC configuration.
- Implemented exact external `super` calls through the provider's reflected MethodBind compatibility hash and the stable GDExtension ABI instead of unsafe cross-library C++ inheritance or guessed signatures.
- Extended ClassDB capture and `gdpp_bridge.json` validation with provider identity, exact callable metadata, load-order-independent provider resolution, cache invalidation, and fail-closed diagnostics when required reflection is unavailable.
- Converted scenes, standalone resources, embedded resources, and Autoloads to attached compiled scripts during binary-only export while retaining provider-owned native types and serialized state.
- Preserved third-party descriptors and target libraries byte for byte in exported games, and made attached startup safe for either provider-first or project-first registry order without requiring customer project or vendor source changes.
- Hardened macOS Universal 2 export with transactional provider-descriptor normalization, per-slice Mach-O validation, crash recovery, and restoration of every source descriptor after export.
- Added an independent dual-GDExtension fixture and real Godot 4.4–4.7 build, load, runtime, export, PCK-content, binary-architecture, and zero-source-leakage gates for the attached delivery path.
- Advanced the packaged SDK to schema 6 and runtime ABI 7, hashing and validating all attached runtime headers and sources for host, desktop, mobile, and Web targets.

## 1.6.0

- Completed the Godot Variant type domain, nullability model, and zero-value truthiness semantics across semantic analysis, typed HIR, generated C++17, and the native runtime.
- Centralized strict assignment, explicit conversion, analyzer compatibility, runtime constructibility, and native storage rules for numeric, string, built-in value, packed array, object, Ref, RID, and container types.
- Added parameterized `as` targets for `Array[T]` and `Dictionary[K,V]`, qualified cross-script enum cast targets, strict constant-cast validation, guarded runtime casts, and compile-time rejection of deterministic String/RID paths that native storage cannot preserve.
- Enforced invariant typed-container storage with exact runtime element, key, value, object-class, and script metadata instead of relying on godot-cpp converting constructors, while preserving GDScript's analyzer acceptance of untyped and packed container boundaries.
- Added stable diagnostics for impossible constant casts, direct invariant typed-container violations, deterministic runtime cast failures, and values such as Object-backed RID expressions that native GDExtension storage cannot preserve.
- Expanded generated-code compilation and real Godot 4.7.1 GDScript/AOT differential coverage for all truthiness categories, built-in conversions, packed arrays, typed-container metadata recovery, String conversion divergence, and RID storage behavior.
- Kept nested warning-control scopes distinct and normalized the analyzer to the pinned formatter, preserving warning-as-error builds across Clang, GCC, and MSVC release gates.
- Restored complete compilation of the pinned rhythm-game and role-playing-game projects after the stricter type work exposed qualified-enum and runtime-container boundary regressions.
- Preserved abstract native script contracts during binary-only export by carrying compiler metadata into ClassDB validation and patching the required runtime registration API into every host, Android, Web, and iOS SDK.
- Advanced the native runtime ABI to 5 for the new guarded conversion and typed-storage contract.

## 1.5.0

- Added end-to-end flow-sensitive typing for `is`/`is not`, null and truthiness checks, short-circuit expressions, `if`/`while`, conditional expressions, post-dominating guards, structural `match`, and guarded bindings.
- Introduced stable symbol identities and a branch-state lattice with conservative joins, reassignment invalidation, callable isolation, and bounded analysis of large logical chains.
- Preserved both effective and storage types plus non-null proofs in typed HIR, allowing the C++ backend to specialize Variant-backed reads without changing their native storage ABI.
- Added source-located null/released-object guards for statically resolved method calls and property reads instead of permitting unchecked native dereferences.
- Completed default-argument lowering and Callable metadata, including omitted-argument dispatch, override compatibility checks, callable arity validation, and exact Godot virtual ABI adapters.
- Implemented the constant and runtime contracts for GDScript language utilities, including `assert`, `is_instance_of`, `type_exists`, `convert`, `str_to_var`, and `var_to_str`.
- Made unresolved values, types, and annotation constants fail closed, and constant-folded exported property hint/usage metadata while preserving Godot's script-variable identity.
- Enforced static-context ownership across fields, methods, accessors, `super`, node shorthand, and nested lambdas, while retaining valid static and inner-class access.
- Sequenced every eager binary operand explicitly so generated C++17 preserves GDScript's left-to-right side-effect order across native, Variant, membership, power, and comparison expressions.
- Expanded strict generated-C++ compilation and real Godot 4.7 GDScript/AOT differential coverage for narrowing, callable ABI, language utilities, annotations, static context, and evaluation order.

## 1.4.0

- Unified frontend constant evaluation, HIR optimization, generated typed C++, and dynamic runtime integer operations behind one portable 64-bit contract.
- Defined deterministic wrapping, normalized shifts, signed division/modulo boundaries, compound assignments, and overflow-safe native range termination without C++ undefined behavior.
- Advanced the packaged SDK to schema 5 and runtime ABI 4, hashing and validating the shared integer-semantics header for every host and export target.
- Added real Godot GDScript/AOT integer differential coverage, strict generated-C++ compilation, and a blocking Linux UBSan core workflow.
- Hardened the Chromium delivery oracle against asynchronous profile writes while preserving strict cleanup failure reporting.

## 1.3.0

- Completed native `for` loop support for floating-point counts, Vector2/Vector2i and Vector3/Vector3i ranges, including direction, step, and boundary behavior matching Godot.
- Added statically typed object iterator support through Godot's `_iter_init`, `_iter_next`, and `_iter_get` protocol, with compile-time validation and typed results.
- Strengthened typed iterator safety by contextually typing array and dictionary literals, precisely diagnosing invalid elements, keys, and iterator declarations.
- Expanded official Godot 4.7 compatibility gates and real Godot GDScript/AOT differential tests for typed, mathematical, container, and custom object iteration.

## 1.2.0

- Added end-to-end native support for typed arrays and dictionaries, preserving their element, key, and value contracts through semantic analysis, cross-script interfaces, and generated C++.
- Enforced typed container safety across assignments, arguments, returns, exported properties, nested containers, and project dependency boundaries.
- Improved generated-code performance by constructing typed container literals directly in their native representation and eliminating redundant wrapper copies.
- Expanded compiler, project, and Godot differential coverage for typed container behavior, ABI stability, dependency invalidation, and runtime performance.

## 1.1.0

- Significantly improved compatibility with the latest GDScript syntax, including strings, numbers, Unicode identifiers, local constants, trailing commas, and lambdas.
- Added complete support for array, dictionary, nested, and rest `match` patterns across type checking, optimization, and native code generation.
- Completed `await` and coroutine compilation with return values, immediate completion, concurrent calls, and dynamic dispatch across inheritance hierarchies.
- Strengthened multi-script project compilation with improved type and call-graph analysis, precise dependency invalidation, and incremental rebuilding.
- Hardened compiler safety and diagnostics with Unicode keyword spoofing protection, malformed-input handling, and configurable frontend resource limits.
- Improved cross-platform export compatibility by inheriting official Godot template settings and added regression coverage against the pinned official Godot 4.7 corpus.

## 1.0.0

- Initial release, featuring a GDScript AOT compiler and Godot editor plugin.
