# stdlib/

Reserved home for a future Swift-side standard library. Nothing in this
directory is loaded at startup or shipped as interpreter behavior today; the
current builtins are implemented in C/assembly under `src/`.

- `core.swift` - future startup declarations (types, builtin shims).
- `prelude.swift` - future user-facing library surface.
