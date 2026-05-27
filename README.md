# T3 Code for Pebble

Pebble Time 2 watch app for browsing a remote T3 Code instance and sending dictated messages to an existing thread.

## Supported Device

- Pebble Time 2 / `emery`

## Setup

1. Expose T3 Code on a reachable address, for example `http://192.168.1.2:3773/`.
2. Generate a pairing token from T3 Code.
3. Open the app settings in the Pebble mobile app.
4. Enter the server URL and pairing code.
5. Tap **Authenticate** and wait for the status to show `Authenticated`.
6. Tap **Save**.

The pairing code is exchanged for a bearer session token and is not needed again unless the server session is revoked.

## Controls

- Project list: `UP` / `DOWN` moves, `SELECT` opens threads.
- Thread list: `UP` / `DOWN` moves, `SELECT` opens messages.
- Message list: `UP` / `DOWN` moves, `SELECT` expands the selected message.
- Message list: long-press `SELECT` starts Pebble dictation and sends the dictated text to the selected thread.
- Expanded message: `UP` / `DOWN` scrolls, `SELECT` returns to the message list.
- `BACK` moves up one level.

## Build

```bash
npm install
pebble build
```

The built app is written to `build/t3code-pebble.pbw`.

## Protocol Notes

The app uses PebbleKitJS for all network access:

- `POST /api/auth/bootstrap/bearer` exchanges the pairing code for a bearer session.
- `POST /api/auth/ws-token` issues a short-lived WebSocket token.
- `/ws?wsToken=...` is used with T3 Code's Effect JSON-RPC protocol.

Only the required RPC subset is implemented:

- `orchestration.subscribeShell` for projects and threads.
- `orchestration.subscribeThread` for thread messages.
- `orchestration.dispatchCommand` for dictated `thread.turn.start` messages.

## Troubleshooting

- `Set server URL`: open settings and enter the T3 Code base URL.
- `Set pairing code`: open settings and enter a fresh pairing token.
- `Invalid bootstrap credential`: generate a new T3 Code pairing token; tokens are one-time credentials.
- `WS TIMEOUT` or `WS ERROR`: confirm the phone can reach the server URL and that T3 Code is still running.
