## 1.5.0

- Added end-to-end flow-sensitive typing for `is`/`is not`, null and truthiness checks, short-circuit expressions, `if`/`while`, conditional expressions, post-dominating guards, structural `match`, and guarded bindings.
- Introduced stable symbol identities and a branch-state lattice with conservative joins, reassignment invalidation, callable isolation, and bounded analysis of large logical chains.
- Preserved both effective and storage types plus non-null proofs in typed HIR, allowing the C++ backend to specialize Variant-backed reads without changing their native storage ABI.
- Added source-located null/released-object guards for statically resolved method calls and property reads instead of permitting unchecked native dereferences.
- Added strict generated-C++ compilation and real Godot 4.7 GDScript/AOT differential coverage for the complete narrowing contract.

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
