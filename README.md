# T3 Code for Pebble

Keep your T3 Code agents moving from your wrist.

T3 Code for Pebble turns a Pebble Time 2 into a compact remote for a T3 Code workspace. Browse projects and threads, check whether an agent is still working, read recent responses, and dictate the next prompt without reaching for a laptop or phone.

It is not a full IDE squeezed onto a 200x228 display. It is a quick, glanceable way to stay in the loop and keep work moving when you are away from your keyboard.

## Install

The app is not yet published in the Pebble Appstore. Download a tagged PBW from [GitHub Releases](https://github.com/tylxr59/t3code-pebble/releases).

## Features

- Browse active T3 Code projects and their non-archived threads.
- See at a glance which projects and threads have an agent working.
- Read recent user and model messages, with a scrollable detail view for longer responses.
- Dictate a prompt into an existing thread or create a new thread from the watch.
- Keep watching an active thread as new responses arrive.
- Use the paired phone as a secure bridge for authentication, network access, and WebSocket traffic.
- Enjoy a button-first interface designed specifically for Pebble Time 2 / `emery`.

## Requirements

- Pebble Time 2 / `emery`
- A paired phone that can reach your T3 Code instance
- A fresh T3 Code pairing code for initial authentication
- Pebble SDK / Pebble Tool, only when building locally

## Screenshots

| Projects | Threads |
| --- | --- |
| ![Projects](screenshots/screenshot_1.png) | ![Threads](screenshots/screenshot_2.png) |

| User message | AI response |
| --- | --- |
| ![Messages](screenshots/screenshot_3.png) | ![Message detail](screenshots/screenshot_4.png) |

## Setup

1. Make your T3 Code instance reachable from the paired phone, for example at `http://192.168.1.2:3773/`.
2. Generate a fresh pairing code. On a headless T3 Code host, run:

   ```bash
   t3 auth pairing create --json
   ```

   Use the returned `credential` value as the pairing code.
3. Open T3 Code for Pebble in the Pebble mobile app and open its settings.
4. Enter the server URL and pairing code.
5. Tap **Authenticate** and wait for the status to show `Authenticated`.
6. Tap **Save**.

The pairing code is exchanged for a bearer session token and then discarded. You only need another pairing code if that session is revoked or expires.

## Controls

- **Projects:** `UP` / `DOWN` selects a project; `SELECT` opens its threads.
- **Threads:** `UP` / `DOWN` selects a thread; `SELECT` opens its messages. Select `+` to start a new thread.
- **Messages:** `UP` / `DOWN` moves through messages; `SELECT` opens the selected message.
- **Message detail:** `UP` / `DOWN` scrolls; `SELECT` returns to the message list.
- **Dictation:** Long-press `SELECT` from the message list, speak your prompt, and confirm it to send.
- **Back:** `BACK` moves up one level or exits from the Projects screen. It remains available while the app is loading.

## Troubleshooting

- `Set server URL`: open settings and enter the base URL of your T3 Code instance.
- `Set pairing code`: open settings and authenticate with a fresh pairing code.
- `Invalid bootstrap credential`: pairing codes are one-time credentials; generate a new one and try again.
- `Set project model in T3 Code`: choose a default model for the project before creating its first thread from the watch.
- `Phone timeout` or `Phone delivery failed`: confirm the Pebble app is connected to the paired phone, then retry.
- `WS TIMEOUT`, `WS CLOSED`, or `WS ERROR`: confirm the phone can reach the server URL and that T3 Code is still running.

## Development

Install dependencies, run the checks, and build the app:

```bash
npm ci
npm run lint
npm run build
```

The PBW is written to `build/t3code-pebble.pbw`.

Install it in the Time 2 emulator with:

```bash
npm run install:emery
```

## Releases

Push a tag matching the version in `package.json`, such as `v0.1.5`, to run the GitHub Actions release workflow. The workflow lints the project, builds the PBW, uploads it as an artifact, and attaches it to the matching GitHub Release.

Release builds pin Pebble Tool 5.0.39 and Pebble SDK 4.17 for reproducibility.

Release notes are taken from the matching version in `CHANGELOG.md`.

To repair the PBW for an existing release, open **Actions → Release PBW → Run workflow** and enter the existing tag. The workflow checks out that exact tag before rebuilding. New code should always receive a new version and tag.

## Project Layout

```text
src/c/                 Pebble C app, state model, and interface
src/pkjs/              Authentication, settings, and WebSocket bridge
screenshots/           Checked-in Pebble Time 2 screenshots
CHANGELOG.md            Versioned user-facing release notes
package.json           Pebble metadata, scripts, and message keys
tools/                  Release-note tooling
wscript                Pebble SDK build script
pebble-appstore.md     Proposed Pebble Appstore listing copy
```

## Protocol Notes

All network access runs through PebbleKitJS on the paired phone:

- `POST /oauth/token` exchanges a one-time pairing code for a bearer access token.
- `POST /api/auth/websocket-ticket` issues a short-lived WebSocket ticket.
- `/ws?wsTicket=...` connects to T3 Code using its tagged JSON RPC envelope protocol.

Legacy bootstrap, WebSocket-token, and `wsToken` routes remain available for older T3 Code releases.

The watch uses a small subset of T3 Code's orchestration API:

- `orchestration.subscribeShell` loads projects and threads, with `orchestration.getArchivedShellSnapshot` as a fallback.
- `orchestration.subscribeThread` loads messages and thread status.
- `orchestration.dispatchCommand` creates threads and sends dictated `thread.turn.start` messages.

## License

T3 Code for Pebble is licensed under the [MIT License](LICENSE).
