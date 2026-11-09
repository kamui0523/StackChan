# Xiaozhi MCP Aggregator

One WebSocket to the official Xiaozhi MCP endpoint; many downstream MCP servers
behind it (music + Home Assistant, etc.).

```text
StackChan ──► Xiaozhi cloud ──► MCP endpoint (single wss)
                                      │
                                      ▼
                              this aggregator
                           ┌──────────┴──────────┐
                           ▼                     ▼
                     music_mcp (stdio)    Home Assistant MCP
                     (+ local mpv)        (HTTP / SSE)
```

Do **not** also run `bridge.py` / `ha-mcp-for-xiaozhi` against the same
endpoint — they would fight this process for the socket. Point only the
aggregator at Xiaozhi; hang HA + music underneath.

## Setup

```bash
cd tools/xiaozhi-mcp-aggregator
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

cp .env.example .env
cp config.example.json config.json
# edit .env → MCP_ENDPOINT
# edit config.json → music cwd + HA url/token
```

### Music (xiaozhi-mcp-music)

1. Clone and install [xiaozhi-mcp-music](https://github.com/ABUGG-007/xiaozhi-mcp-music) elsewhere.
2. In **that** project, set `LOCAL_PLAYER=mpv` / `AUTO_PLAY_LOCAL=true` if you want PC playback.
3. Do **not** run its `bridge.py`.
4. In `config.json`, set `music.cwd` to that repo path (and `python -m music_mcp` must work there).

### Home Assistant

1. In HA: install the official **MCP Server** integration (or expose an MCP HTTP/SSE URL).
2. Create a long-lived access token.
3. Put the MCP URL + `Authorization: Bearer …` into `config.json` → `homeassistant`.
4. Expose the entities you want under **Settings → Voice assistants → Expose**.
5. Do **not** enable `ha-mcp-for-xiaozhi` against the same Xiaozhi endpoint while using this aggregator.

> HA MCP URL paths differ by version/integration. If unsure, check the HA MCP Server
> docs for your install; common forms are `/api/mcp` or an SSE endpoint from the
> MCP Server integration UI.

## Run

```bash
source .venv/bin/activate
python aggregator.py
```

Log should show downstream tool counts, then `Connected to Xiaozhi MCP endpoint`.
In the Xiaozhi console MCP page, refresh — tools appear as `music.*`, `ha.*`, plus `hub.status`.

Keep this process running (launchd / tmux / Docker later if you want).

## Tool naming

Downstream tools are exposed as `{namespace}.{original_name}` so they never collide.
Ask the model to call e.g. `music.play_song` or HA tools under `ha.*`. Paste a short
role hint listing those names if the agent ignores them.

## Limits

- Speaks `initialize` / `tools/list` / `tools/call` / `ping` to Xiaozhi.
- Resources / prompts are not aggregated yet (music ResourceLink streaming to the
  robot still needs device/firmware support; `LOCAL_PLAYER=mpv` is for PC audio).
- Remote HTTP/SSE backends are bridged through `python -m mcp_proxy`.
