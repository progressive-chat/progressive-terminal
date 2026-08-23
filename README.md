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

## Multi-account and per-account proxy

`progressive-terminal` supports several accounts at once. Each `register` /
`session` stores an account under a name (default `default`) together with its
`host`, `session`, and a per-account `proxy`:

```
~/.config/progressive-terminal/accounts/<name>     # {host, session, proxy}
~/.config/progressive-terminal/current            # active account name
```

This is the **only** state the client keeps on disk — no message database, no
Matrix data. The `proxy` is per-account and forwarded to the server, which
applies it to that account's homeserver traffic (overriding the server-wide
default). Use `off` for a direct connection.

```bash
# Create / save accounts (each with its own proxy):
progressive-terminal register --name alice --homeserver https://a.org \
    --username alice --password secret --proxy off
progressive-terminal session  --name bob   --homeserver https://b.org \
    --user @bob:b.org --token TOK --device DEV --proxy socks5://127.0.0.1:9050

progressive-terminal accounts          # list, '*' marks the active one
progressive-terminal use alice         # switch active account
progressive-terminal render --static   # uses the active account
progressive-terminal render --account bob   # or pick one explicitly
progressive-terminal logout --account bob    # forget one
progressive-terminal logout --all           # forget everything
```

`render` / `input` / `sync` default to the active account unless `--account`
is given. The active account's host also acts as a `PROGTERM_HOST` fallback.

The server side (`progressive-cli serve --ttys`) still holds all real state; by
default its database is in-memory (`:memory:`), so a session lives in the
server's RAM and is lost when that server restarts.

## Auto-connect (port scan)

When no host is given explicitly (`--host` / `PROGTERM_HOST` / cached account
host are all absent), the client scans `127.0.0.1` ports near a base port and
connects to the first `progressive-cli serve --ttys` relay it finds. The scan
expands outward from the base (base, +1, −1, +2, …) up to `--scan-range`
(default 10), so a relay running on `29325 ± N` is discovered without any
configuration:

```bash
progressive-terminal register --name alice \
    --homeserver https://example.org --username alice --password secret
# ^ no --host: auto-scans 127.0.0.1:29315..29335, connects to the relay

progressive-terminal render --static   # also auto-discovers when needed
```

Tuning / disabling:

```bash
progressive-terminal register ... --scan-base 29325 --scan-range 20
progressive-terminal render --no-scan   # never scan; require an explicit host
```

The scan only probes localhost, so it is fast and safe; a found relay is
remembered per-account after the first successful `register`/`session`.

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
