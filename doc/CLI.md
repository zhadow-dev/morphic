# Morphic CLI

Morphic ships a small command-line tool with the package, invoked as
`dart run morphic:<command>` from your Flutter project.

## Native runtime — no account, no network

```bash
flutter pub add morphic
dart run morphic:init          # transform windows/ into the multi-surface runtime
flutter run -d windows
```

`morphic:init` is fully standalone — it installs the native multi-window
runtime into your Windows runner. No sign-in, no backend, no internet required.
This is all most apps need.

## Licensing CLI — experimental

The premium **spatial** runtime is delivered through an authenticated flow.
These commands talk to a **Morphic backend**, so they're opt-in and require you
to point them at one:

```bash
# Set these before using the licensing commands (they have no public default):
export MORPHIC_API_URL="https://www.getmorphic.space/api"
export MORPHIC_SITE_URL="https://www.getmorphic.space"   # hosts the browser login page
```

| Command | What it does |
| --- | --- |
| `dart run morphic:login` | Opens your browser, signs you in, stores a refresh token locally. Use `--email you@example.com` for a dev/mock backend (skips the browser). |
| `dart run morphic:whoami` | Shows the signed-in account, plan and spatial access. |
| `dart run morphic:license` | Shows your license: tier (Developer Preview), projects, spatial access and activation status. |
| `dart run morphic:logout` | Revokes the session and clears local credentials. |
| `dart run morphic:init --spatial` | Authenticated secure delivery of the spatial runtime: **authorize → short-TTL signed URL → download → SHA-256 verify → install**, then runs `init`. |

Credentials are stored per-OS:

| OS | Path |
| --- | --- |
| Windows | `%APPDATA%\Morphic\auth.json` |
| macOS | `~/Library/Application Support/Morphic/auth.json` |
| Linux | `$XDG_CONFIG_HOME/morphic/auth.json` (or `~/.config/morphic/`) |

A long-lived **refresh token** is stored; short-lived access tokens are fetched
on demand, so a login keeps working across sessions.

> **Status:** the licensing / spatial-delivery flow is experimental and needs a
> running Morphic backend. Without `MORPHIC_API_URL` set, `morphic:login` has no
> endpoint to reach. The native `morphic:init` path above is unaffected.
