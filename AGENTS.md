# AGENTS

## Build

```bash
cmake -S . -B build && cmake --build build
# minimal/fast build (no interactive TUI):
cmake -S . -B build -DBUILD_TUI=OFF && cmake --build build
```

`BUILD_TUI=OFF` omits `src/tui.cpp` entirely — use it when you only need the
static curl-wrapper, or to compile faster.

## Conventions

- **English only** in all committed content: code, comments, commit messages,
  docs. No other languages.
- Keep the binary dependency-light: only `libcurl` is required. Do not pull in
  the Matrix core here — that lives on the `progressive-cli` relay side.
- `src/json.cpp` is a deliberately tiny JSON helper (string extraction only).
  Extend it there rather than adding a full JSON library.

## Workflow

- Commit and push each logical change separately, with a clear English summary.
- Verify the build (`cmake --build build`) before committing.
- There is no interactive TUI here anymore; it lives on the `dumb`
  branch (`progterm term`). Keep this branch command-only.
