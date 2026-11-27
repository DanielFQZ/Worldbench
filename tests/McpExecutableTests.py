"""Verify a real Worldbench executable through its real stdio bridge in isolation."""
import argparse
import base64
import json
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=Path)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
checks = []
calls = {}

def check(condition, message):
    if not condition: raise AssertionError(message)
    checks.append(message)

class Bridge:
    def __init__(self, configuration):
        self.process = subprocess.Popen([configuration["command"], *configuration["args"]],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                        text=True, encoding="utf-8", bufsize=1)
        self.incoming = queue.Queue()
        self.serial = 0
        def read():
            for line in self.process.stdout:
                self.incoming.put(json.loads(line))
        threading.Thread(target=read, daemon=True).start()
        initialized = self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "Worldbench release verification", "version": "1"}})
        check(initialized["result"]["protocolVersion"] == "2025-06-18", "protocol negotiation")
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def send(self, message):
        self.process.stdin.write(json.dumps(message, ensure_ascii=False) + "\n")
        self.process.stdin.flush()

    def request(self, method, params):
        self.serial += 1
        self.send({"jsonrpc": "2.0", "id": self.serial, "method": method, "params": params})
        reply = self.incoming.get(timeout=45)
        check(reply.get("id") == self.serial and "error" not in reply, "JSON-RPC response identity")
        return reply

    def call(self, name, arguments=None, error=False):
        calls[name] = calls.get(name, 0) + 1
        result = self.request("tools/call", {"name": name, "arguments": arguments or {}})["result"]
        check(bool(result.get("isError")) == error, f"{name}: expected {'rejection' if error else 'success'}: {str(result)[:240] if result.get('isError') != error else ''}")
        content = result["content"][0]
        if error or content["type"] == "image": return content
        return json.loads(content["text"])

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)

with tempfile.TemporaryDirectory(prefix="Worldbench-executable-mcp-") as temporary:
    base = Path(temporary)
    profile = base / "profile"
    profile.mkdir()
    bridge = None
    with (base / "process.log").open("wb") as log:
        application = subprocess.Popen([str(args.executable.resolve()), "--automation-test", str(profile)], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 45
            while not (profile / "mcp.json").exists():
                check(application.poll() is None, "executable remains running during startup")
                if time.monotonic() > deadline: raise TimeoutError("MCP configuration not produced")
                time.sleep(.1)
            configuration = json.loads((profile / "mcp.json").read_text(encoding="utf-8"))["mcpServers"]["worldbench"]
            bridge = Bridge(configuration)
            tools = bridge.request("tools/list", {})["result"]["tools"]
            check(len(tools) == 16, "all 16 MCP tools discoverable")
            environment_description = next(t["description"] for t in tools if t["name"] == "environment")
            check("DDS and KTX2 are not supported" in environment_description, "sky format description matches implementation")
            def ready(mesh=False):
                end = time.monotonic() + 45
                while True:
                    info = bridge.call("world_info")
                    if not info["busy"] and (not mesh or not info["mesh_busy"]): return info
                    if time.monotonic() > end: raise TimeoutError("Editor did not settle")
                    time.sleep(.1)
            def mutate(name, values=None, error=False):
                return bridge.call(name, {**(values or {}), "expected_revision": ready()["revision"]}, error)
            ready(mesh=True)
            check(bridge.call("catalog", {"query": "stone", "limit": 10})["matches"] > 0, "block catalog query")
            before = ready()["revision"]
            mutate("set_blocks", {"blocks": [{"position": [0, 100, 0], "block": "minecraft:glass"}]})
            region = bridge.call("read_region", {"min": [0, 100, 0], "max": [0, 100, 0]})
            check(region["palette"][region["blocks"][0]] == "minecraft:glass", "MCP write/read round trip")
            bridge.call("undo", {"expected_revision": before}, error=True)
            mutate("fill_box", {"min": [1, 100, 1], "max": [2, 100, 2], "block": "minecraft:stone"})
            mutate("undo")
            empty = bridge.call("read_region", {"min": [1, 100, 1], "max": [2, 100, 2]})
            check(all(empty["palette"][i] == "minecraft:air" for i in empty["blocks"]), "one undo removes fill")
            mutate("redo")
            filled = bridge.call("read_region", {"min": [1, 100, 1], "max": [2, 100, 2]})
            check(all(filled["palette"][i] == "minecraft:stone" for i in filled["blocks"]), "redo restores fill")
            mutate("sculpt_layer", {"action": "add"})
            mutate("sculpt", {"position": [4, 30, 4], "tool": 0, "radius": 1, "strength": 1})
            job = mutate("height_region", {"min": [-2, -2], "max": [2, 2], "offset": 1})
            deadline = time.monotonic() + 30
            while job["state"] not in ("completed", "failed", "cancelled"):
                if time.monotonic() > deadline: raise TimeoutError("region job")
                time.sleep(.05)
                job = bridge.call("region_job", {"job_id": job["job_id"], "action": "status"})
            check(job["state"] == "completed", "asynchronous height job completes")
            mutate("environment", {"changes": {"skyIntensity": 110}})
            mutate("terrain_instance", {"id": "missing-instance", "changes": {"x": 1}}, error=True)
            created = mutate("creation", {"action": "new_graph", "name": "MCP release test", "surface_material": True})
            graph_id = created["creation"]["graphs"][-1]["id"]
            mutate("creation", {"action": "new_volume", "graph_id": graph_id})
            asset = {"id": "mcp-test-asset", "name": "MCP asset", "width": 4, "height": 4, "depth": 4,
                     "pivot": [0, 0, 0], "category": "closed", "blocks": [{"x": 0, "y": 0, "z": 0, "state": "minecraft:stone"}]}
            mutate("creation", {"action": "edit", "operation": "add", "data": {"kind": "assets", "changes": asset}})
            saved_revision = ready()["revision"]
            mutate("creation", {"action": "edit", "operation": "patch", "data": {"kind": "assets", "id": asset["id"],
                    "changes": {"blocks": [{"x": "bad", "y": 0, "z": 0, "state": "minecraft:stone"}]}}}, error=True)
            check(ready()["revision"] == saved_revision, "invalid asset input is atomic")
            mutate("creation", {"action": "cave_analysis", "asset_id": asset["id"]})
            mutate("creation", {"action": "place_asset", "asset_id": asset["id"], "x": 8, "y": 100, "z": 8})
            mutate("unknown_tool", error=True)
            bridge.call("camera", {"position": [26, 119, 30], "yaw": -35, "pitch": -28})
            ready(mesh=True)
            time.sleep(.3)
            capture = bridge.call("screenshot")
            png = base64.b64decode(capture["data"], validate=True)
            check(png.startswith(b"\x89PNG\r\n\x1a\n") and len(png) > 1000, "real executable viewport screenshot")
            bridge.stop()
            check(application.poll() is None, "disconnect does not terminate executable")
            bridge = Bridge(configuration)
            check(bridge.call("world_info")["revision"] >= saved_revision, "reconnect reads preserved world")
            report = {"executable": str(args.executable.resolve()), "passed": True, "checks": len(checks),
                      "discovered_tools": [t["name"] for t in tools], "tool_calls": calls,
                      "read_only_error_cases": ["terrain_instance: missing id", "unknown_tool"],
                      "profile_isolated": True, "screenshot_bytes": len(png)}
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                args.output.with_suffix(".png").write_bytes(png)
            print(json.dumps(report, ensure_ascii=False))
        except Exception:
            log.flush()
            print((base / "process.log").read_text(encoding="utf-8", errors="replace")[-5000:])
            raise
        finally:
            if bridge: bridge.stop()
            if application.poll() is None:
                application.terminate()
                application.wait(timeout=10)
