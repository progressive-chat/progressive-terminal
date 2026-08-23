# progressive-terminal

Lightweight C++ client for **progressive-chat (cli edition)**.

It is a **smart `curl` wrapper**: it never embeds the Matrix stack. Instead it
talks over plain HTTP to a `progressive-cli serve --ttys` relay and renders the
server's ASCII UI on your terminal. That keeps the local binary small and fast
to build — perfect when you want the remote ASCII experience without shipping
the heavy client.

## What it does

- **Detects your terminal size** (`TIOCGWINSZ` → `COLUMNS`/`LINES` → 80×24).
- Sends it as `term:{cols,rows}` in the request.
- Renders the server's ASCII UI frame and prints it.
- Offers the full flow without a big binary: `register`, `session`, `input`,
  `sync` over the same HTTP API.

## Build

```bash
cmake -S . -B build
cmake --build build
```

### Compile-time TUI toggle

The interactive TUI lives in `src/tui.cpp` and is compiled in **only** when
`BUILD_TUI=ON` (the default). Turn it off to build just the static curl wrapper
— smaller and faster to compile:

```bash
cmake -S . -B build -DBUILD_TUI=OFF
cmake --build build
```

When `BUILD_TUI=OFF`, `progressive-terminal render --tui` is unavailable and the
binary contains only the request/print path.

## Usage

```bash
# Point at a running relay (or set PROGTERM_HOST):
export PROGTERM_HOST=http://127.0.0.1:29325

# 1. Register a fresh account (prints a session id + credentials):
progressive-terminal register \
    --homeserver https://example.org --username newuser --password secret

# 2. Or reuse an existing account:
progressive-terminal session \
    --homeserver https://example.org --user @me:example.org \
    --token TOKEN --device DEV

# 3. Render the ASCII UI (auto-detects your terminal size):
progressive-terminal render --session "@a2:mock.local" --static

# 4. Interactive mode (only in a BUILD_TUI build):
progressive-terminal render --session "@a2:mock.local" --tui
```

## Server side

You still need a `progressive-cli` relay somewhere (a small box, a container, a
remote host) running:

```bash
progressive-cli serve --ttys --port 29325
```

`progressive-terminal` is purely the client.

## License

See repository settings.
