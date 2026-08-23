# progressive-terminal (lite)

A **zero-option pipe** to a `progressive-cli serve --ttys` relay.
One request per invocation: the path is forwarded verbatim, the body is
sent exactly as typed, the response goes to stdout untouched. The client
stores nothing anywhere.

```
progterm register <homeserver> <user> <password>  # create + remember session
progterm last                                     # re-sync remembered id
progterm render [session] [room]                  # static frame, auto-sized
progterm term [session] [room]                    # remote-terminal loop
progterm sync [session] | proxy [on <preset>|off]
progterm <path> [json-body]                       # raw pipe (everything else)
```

`<session>` arguments are OPTIONAL: the active PROFILE's session is used
(override: `$PROGTERM_SESSION`, `$PROGTERM_SESSION_FILE`). `register`
and `last` write that memory automatically.

### Profile import / export

```bash
progterm-lite profile export phone.bundle   # chmod 600 — carries sessions!
progterm-lite profile import phone.bundle    # upsert by name, merge-safe
```

The bundle is a plain text snapshot of every profile plus the `current`
pointer — human-readable, diffable, and byte-stable on round-trip. It
carries session ids, so treat it like a secret.

**Offline catch (on by default):** every POST that cannot reach the relay
is spooled to `~/.config/progterm-lite/outbox` and auto-delivered on the
next successful contact — including lines typed in a `term` session while
offline. Tune it with `PROGTERM_OUTBOX`: another file, or an empty value
to disable spooling entirely.

Environment:

| Variable | Meaning |
|---|---|
| `PROGTERM_HOST` | relay address; else scan 127.0.0.1 near `:29325`; else `http://127.0.0.1:29325` |
| `PROGTERM_TOKEN` | sent as `Authorization: Bearer` when set |
| `PROGTERM_OUTBOX` | optional spool file for offline POSTs (see above) |

Exit codes: `0` = 2xx, `1` = HTTP error (body still printed), `2` = no route
to the relay.

## Examples

```bash
export PROGTERM_TOKEN=secret

# relay-side proxy control
progterm api/ttys/proxy '{"action":"on","preset":"tor"}'
progterm api/ttys/proxy                      # status (GET)

# bootstrap: ask the RELAY for the latest session (nothing is stored on
# this device; after any restart just run this line again)
S=$(progterm api/ttys/session/last | jq -r .session)

# or create a brand-new account from the pipe (one-off, home-side usually)
# S=$(progterm api/ttys/register "$(cat reg.json)" | jq -r .session)

# one input line
progterm api/ttys/input "{\"session\":\"$S\",\"input\":\"hello\"}"

# static ASCII frame, sized automatically to your terminal
progterm render "$S" | jq -r .frame
```

## Build

```bash
cmake -S . -B build && cmake --build build
```

Dependencies: C++20 and libcurl — nothing else. No config files, no cache,
no state on disk.

## Relationship to the main branch

The `main` branch of this repository is the sugar edition: named commands,
profiles with per-profile proxy, terminal-size detection and an interactive
TUI. This `dumb` branch is the same protocol with none of that — a curl that
knows your relay address. Pick per taste; both speak identical wire.

## License

See repository settings.
