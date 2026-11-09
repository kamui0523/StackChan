#!/usr/bin/env python3
"""
Xiaozhi MCP aggregator: one WebSocket to the official MCP endpoint, many
downstream MCP servers (stdio / HTTP / SSE) behind it.

Unlike running multiple mcp_pipe connections against the same endpoint (which
fight over a single socket), this process:
  1. connects once to MCP_ENDPOINT
  2. speaks MCP server protocol to Xiaozhi
  3. acts as MCP client to each configured downstream
  4. merges tools/list and routes tools/call by namespace prefix
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import signal
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import websockets
from dotenv import load_dotenv

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger("mcp-aggregator")

INITIAL_BACKOFF = 1.0
MAX_BACKOFF = 600.0
PROTOCOL_VERSION = "2024-11-05"


def _json_line(obj: dict[str, Any]) -> str:
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":"))


@dataclass
class DownstreamServer:
    name: str
    namespace: str
    cmd: list[str]
    env: dict[str, str]
    cwd: str | None = None
    process: asyncio.subprocess.Process | None = field(default=None, repr=False)
    reader_task: asyncio.Task | None = field(default=None, repr=False)
    pending: dict[int, asyncio.Future] = field(default_factory=dict, repr=False)
    next_id: int = 1
    tools: list[dict[str, Any]] = field(default_factory=list)
    # exposed_name -> original tool name
    tool_map: dict[str, str] = field(default_factory=dict)

    def alloc_id(self) -> int:
        rid = self.next_id
        self.next_id += 1
        return rid


class Aggregator:
    def __init__(self, endpoint: str, servers_cfg: dict[str, Any]):
        self.endpoint = endpoint
        self.servers_cfg = servers_cfg
        self.servers: dict[str, DownstreamServer] = {}
        # exposed tool name -> DownstreamServer
        self.route: dict[str, DownstreamServer] = {}

    async def start_downstreams(self) -> None:
        for name, entry in self.servers_cfg.items():
            entry = entry or {}
            if entry.get("disabled"):
                logger.info("Skipping disabled server: %s", name)
                continue
            server = self._build_server(name, entry)
            await self._spawn_stdio(server)
            await self._handshake_downstream(server)
            self.servers[name] = server
            logger.info(
                "Downstream ready: %s (%d tools, namespace=%s)",
                name,
                len(server.tools),
                server.namespace,
            )
        if not self.servers:
            raise RuntimeError("No enabled downstream mcpServers in config")
        self._rebuild_route()

    def _build_server(self, name: str, entry: dict[str, Any]) -> DownstreamServer:
        namespace = str(entry.get("namespace") or name)
        typ = (entry.get("type") or entry.get("transportType") or "stdio").lower()
        child_env = os.environ.copy()
        for k, v in (entry.get("env") or {}).items():
            child_env[str(k)] = str(v)
        cwd = entry.get("cwd")

        if typ == "stdio":
            command = entry.get("command")
            args = entry.get("args") or []
            if not command:
                raise RuntimeError(f"Server '{name}' missing command")
            cmd = [str(command), *[str(a) for a in args]]
        elif typ in ("sse", "http", "streamablehttp", "streamable_http"):
            url = entry.get("url")
            if not url:
                raise RuntimeError(f"Server '{name}' (type {typ}) missing url")
            # mcp_proxy turns remote HTTP/SSE MCP into local stdio.
            cmd = [sys.executable, "-m", "mcp_proxy"]
            if typ in ("http", "streamablehttp", "streamable_http"):
                cmd += ["--transport", "streamablehttp"]
            for hk, hv in (entry.get("headers") or {}).items():
                cmd += ["-H", str(hk), str(hv)]
            cmd.append(str(url))
        else:
            raise RuntimeError(f"Unsupported server type for '{name}': {typ}")

        return DownstreamServer(
            name=name, namespace=namespace, cmd=cmd, env=child_env, cwd=cwd
        )

    async def _spawn_stdio(self, server: DownstreamServer) -> None:
        logger.info("Starting %s: %s", server.name, " ".join(server.cmd))
        server.process = await asyncio.create_subprocess_exec(
            *server.cmd,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            env=server.env,
            cwd=server.cwd,
        )
        server.reader_task = asyncio.create_task(self._read_stdout(server))
        asyncio.create_task(self._read_stderr(server))

    async def _read_stderr(self, server: DownstreamServer) -> None:
        assert server.process and server.process.stderr
        while True:
            line = await server.process.stderr.readline()
            if not line:
                break
            text = line.decode("utf-8", errors="replace").rstrip()
            if text:
                logger.info("[%s][stderr] %s", server.name, text)

    async def _read_stdout(self, server: DownstreamServer) -> None:
        assert server.process and server.process.stdout
        while True:
            line = await server.process.stdout.readline()
            if not line:
                logger.warning("[%s] stdout closed", server.name)
                break
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                msg = json.loads(text)
            except json.JSONDecodeError:
                logger.warning("[%s] non-JSON stdout: %s", server.name, text[:200])
                continue
            msg_id = msg.get("id")
            if msg_id is not None and msg_id in server.pending:
                fut = server.pending.pop(msg_id)
                if not fut.done():
                    fut.set_result(msg)
            else:
                logger.debug("[%s] unsolicited: %s", server.name, text[:160])

    async def _request(
        self, server: DownstreamServer, method: str, params: dict[str, Any] | None = None
    ) -> dict[str, Any]:
        assert server.process and server.process.stdin
        rid = server.alloc_id()
        payload: dict[str, Any] = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            payload["params"] = params
        fut: asyncio.Future = asyncio.get_running_loop().create_future()
        server.pending[rid] = fut
        server.process.stdin.write((_json_line(payload) + "\n").encode("utf-8"))
        await server.process.stdin.drain()
        try:
            return await asyncio.wait_for(fut, timeout=60)
        except asyncio.TimeoutError as exc:
            server.pending.pop(rid, None)
            raise RuntimeError(f"{server.name} timeout on {method}") from exc

    async def _notify(
        self, server: DownstreamServer, method: str, params: dict[str, Any] | None = None
    ) -> None:
        assert server.process and server.process.stdin
        payload: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        server.process.stdin.write((_json_line(payload) + "\n").encode("utf-8"))
        await server.process.stdin.drain()

    async def _handshake_downstream(self, server: DownstreamServer) -> None:
        init = await self._request(
            server,
            "initialize",
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {"name": "xiaozhi-mcp-aggregator", "version": "0.1.0"},
            },
        )
        if "error" in init:
            raise RuntimeError(f"{server.name} initialize failed: {init['error']}")
        await self._notify(server, "notifications/initialized")

        tools_resp = await self._request(server, "tools/list", {})
        if "error" in tools_resp:
            raise RuntimeError(f"{server.name} tools/list failed: {tools_resp['error']}")
        tools = (tools_resp.get("result") or {}).get("tools") or []
        server.tools = tools
        server.tool_map = {}
        for tool in tools:
            origin = tool.get("name") or ""
            exposed = f"{server.namespace}.{origin}"
            server.tool_map[exposed] = origin

    def _rebuild_route(self) -> None:
        self.route.clear()
        for server in self.servers.values():
            for exposed in server.tool_map:
                if exposed in self.route:
                    logger.warning("Duplicate tool name %s; later server wins", exposed)
                self.route[exposed] = server

    def merged_tools(self) -> list[dict[str, Any]]:
        out: list[dict[str, Any]] = [
            {
                "name": "hub.status",
                "description": "Aggregator health: list connected downstream MCP servers and tool counts.",
                "inputSchema": {"type": "object", "properties": {}},
            }
        ]
        for server in self.servers.values():
            for tool in server.tools:
                origin = tool.get("name") or ""
                exposed = f"{server.namespace}.{origin}"
                cloned = dict(tool)
                cloned["name"] = exposed
                desc = cloned.get("description") or ""
                cloned["description"] = f"[{server.namespace}] {desc}".strip()
                out.append(cloned)
        return out

    async def stop_downstreams(self) -> None:
        for server in self.servers.values():
            if server.reader_task:
                server.reader_task.cancel()
            proc = server.process
            if proc and proc.returncode is None:
                proc.terminate()
                try:
                    await asyncio.wait_for(proc.wait(), timeout=5)
                except asyncio.TimeoutError:
                    proc.kill()

    async def handle_xiaozhi_message(self, websocket, raw: str | bytes) -> None:
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", errors="replace")
        raw = raw.strip()
        if not raw:
            return
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            logger.warning("Non-JSON from Xiaozhi: %s", raw[:200])
            return

        method = msg.get("method")
        msg_id = msg.get("id")

        # Notifications from cloud — ignore.
        if method and msg_id is None:
            logger.debug("Xiaozhi notify: %s", method)
            return

        if method == "initialize":
            result = {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "xiaozhi-mcp-aggregator", "version": "0.1.0"},
            }
            await websocket.send(_json_line({"jsonrpc": "2.0", "id": msg_id, "result": result}))
            return

        if method == "notifications/initialized":
            return

        if method == "ping":
            await websocket.send(_json_line({"jsonrpc": "2.0", "id": msg_id, "result": {}}))
            return

        if method == "tools/list":
            await websocket.send(
                _json_line(
                    {
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "result": {"tools": self.merged_tools()},
                    }
                )
            )
            return

        if method == "tools/call":
            params = msg.get("params") or {}
            name = params.get("name") or ""
            arguments = params.get("arguments") or {}
            try:
                result = await self._call_tool(name, arguments)
                await websocket.send(
                    _json_line({"jsonrpc": "2.0", "id": msg_id, "result": result})
                )
            except Exception as exc:
                logger.exception("tools/call %s failed", name)
                await websocket.send(
                    _json_line(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "error": {"code": -32000, "message": str(exc)},
                        }
                    )
                )
            return

        if method:
            await websocket.send(
                _json_line(
                    {
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "error": {"code": -32601, "message": f"Method not found: {method}"},
                    }
                )
            )

    async def _call_tool(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        if name == "hub.status":
            body = {
                "endpoint_connected": True,
                "servers": [
                    {
                        "name": s.name,
                        "namespace": s.namespace,
                        "tools": len(s.tools),
                        "exposed": sorted(s.tool_map.keys()),
                    }
                    for s in self.servers.values()
                ],
            }
            return {
                "content": [{"type": "text", "text": json.dumps(body, ensure_ascii=False)}],
                "isError": False,
            }

        server = self.route.get(name)
        if not server:
            raise RuntimeError(f"Unknown tool: {name}")
        origin = server.tool_map[name]
        resp = await self._request(
            server,
            "tools/call",
            {"name": origin, "arguments": arguments},
        )
        if "error" in resp:
            err = resp["error"]
            raise RuntimeError(err.get("message") if isinstance(err, dict) else str(err))
        return resp.get("result") or {
            "content": [{"type": "text", "text": ""}],
            "isError": False,
        }

    async def run_forever(self) -> None:
        backoff = INITIAL_BACKOFF
        attempt = 0
        while True:
            try:
                if attempt:
                    logger.info("Reconnect in %.0fs (attempt %d)", backoff, attempt)
                    await asyncio.sleep(backoff)
                logger.info("Connecting to Xiaozhi MCP endpoint...")
                async with websockets.connect(self.endpoint, max_size=8 * 1024 * 1024) as ws:
                    logger.info("Connected to Xiaozhi MCP endpoint")
                    attempt = 0
                    backoff = INITIAL_BACKOFF
                    async for message in ws:
                        await self.handle_xiaozhi_message(ws, message)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                attempt += 1
                logger.warning("Xiaozhi connection ended: %s", exc)
                backoff = min(backoff * 2, MAX_BACKOFF)


def load_config() -> dict[str, Any]:
    path = os.environ.get("MCP_CONFIG")
    if not path:
        path = str(Path(__file__).resolve().parent / "config.json")
    cfg_path = Path(path)
    if not cfg_path.exists():
        raise RuntimeError(
            f"Config not found: {cfg_path}. Copy config.example.json to config.json"
        )
    with cfg_path.open("r", encoding="utf-8") as f:
        if cfg_path.suffix.lower() in (".yaml", ".yml"):
            import yaml

            return yaml.safe_load(f) or {}
        return json.load(f)


async def amain() -> None:
    endpoint = os.environ.get("MCP_ENDPOINT", "").strip()
    if not endpoint or "YOUR_TOKEN" in endpoint:
        raise RuntimeError("Set MCP_ENDPOINT in .env to your Xiaozhi MCP access point")

    cfg = load_config()
    servers = cfg.get("mcpServers") or {}
    if not isinstance(servers, dict) or not servers:
        raise RuntimeError("config.mcpServers is empty")

    agg = Aggregator(endpoint, servers)
    await agg.start_downstreams()
    logger.info(
        "Exposing %d tools to Xiaozhi: %s",
        len(agg.merged_tools()),
        ", ".join(t["name"] for t in agg.merged_tools()),
    )

    stop = asyncio.Event()

    def _stop(*_args):
        stop.set()

    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            signal.signal(sig, lambda *_: _stop())

    run_task = asyncio.create_task(agg.run_forever())
    await stop.wait()
    run_task.cancel()
    try:
        await run_task
    except asyncio.CancelledError:
        pass
    await agg.stop_downstreams()
    logger.info("Shutdown complete")


def main() -> None:
    try:
        asyncio.run(amain())
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        logger.error("%s", exc)
        sys.exit(1)


if __name__ == "__main__":
    main()
