# Agent Notes

This repo is a Pebble Time 2 app for a remote T3 Code instance.

For local live tests, read `.env` if present. It is intentionally git-ignored and may contain:

- `T3CODE_BASE_URL`, for example `http://192.168.1.2:3773`
- `T3CODE_BEARER_TOKEN`
- `T3CODE_PAIRING_CODE`, usually already consumed

Do not wire `.env` into the Pebble app bundle. The app gets credentials from the phone settings page, where the user enters the server URL and one-time pairing code, taps Authenticate, then saves the returned bearer token into PebbleKitJS localStorage.

Generate a fresh pairing code on a headless host with:

```bash
t3 auth pairing create --json
```

Use the returned `credential` value. Useful T3 Code calls:

- Pairing exchange: `POST /oauth/token` using the OAuth token-exchange form fields.
- WebSocket ticket: `POST /api/auth/websocket-ticket` with `Authorization: Bearer TOKEN`.
- WebSocket URL: `/ws?wsTicket=SHORT_LIVED_TICKET`.

Keep legacy fallbacks for `POST /api/auth/bootstrap/bearer`, `POST /api/auth/ws-token`, and
`/ws?wsToken=...` so the watch app remains compatible with older T3 releases.

The WebSocket transport uses T3 Code's tagged JSON RPC envelopes. Send requests like:

```json
{"_tag":"Request","id":"1","tag":"orchestration.getArchivedShellSnapshot","payload":{},"headers":[]}
```

Responses are `_tag: "Exit"` for completed calls and `_tag: "Chunk"` plus `_tag: "Exit"` for streams. Ack stream chunks with:

```json
{"_tag":"Ack","requestId":"1"}
```

For project and thread lists, prefer live `orchestration.subscribeShell` and close after the first snapshot. Do not switch this to unary-first `orchestration.getArchivedShellSnapshot`: in this app it has twice regressed the watch to showing only archived/limited projects such as `PebbleGetMe` and `Pinhole`, with no useful thread list. Keep `getArchivedShellSnapshot` only as a fallback if `subscribeShell` errors. For thread messages, use `orchestration.subscribeThread` and close after the first snapshot.
