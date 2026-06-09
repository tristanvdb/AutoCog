"""Extra CLI coverage for autocog/__main__.py.

Covers paths the in-process server tests miss:
  * the ECS --json logging handler (setup_logging) and main()'s --json
    except-handler arms (AutoCogError / JSONDecodeError),
  * the cmd_backend / cmd_rpc / cmd_serve handlers, which call uvicorn.run and
    so are exercised here by launching `python -m autocog <cmd>` as a subprocess.
"""

import json
import socket
import subprocess
import sys
import time
import urllib.request

import pytest


def _is_json_object(line):
    try:
        return isinstance(json.loads(line), dict)
    except Exception:
        return False


def _free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class TestCLIJsonMode:
    """The global --json flag switches logging/errors to ECS NDJSON."""

    def test_json_error_mode_emits_ndjson(self):
        """A failing command under --json emits NDJSON on stderr and exits 1."""
        r = subprocess.run(
            [sys.executable, "-m", "autocog", "--json",
             "compile", "--stl", "/nonexistent/program.stl"],
            capture_output=True, text=True, timeout=30,
        )
        assert r.returncode == 1
        lines = [l for l in r.stderr.splitlines() if l.strip()]
        assert lines, "expected NDJSON log output on stderr"
        assert any(_is_json_object(l) for l in lines), \
            f"no JSON record on stderr: {r.stderr!r}"

    def test_json_invalid_input(self, repo_root):
        """--json with malformed --input JSON hits the JSONDecodeError --json arm."""
        stl = str(repo_root / "share/demos/mcq/select.stl")
        r = subprocess.run(
            [sys.executable, "-m", "autocog", "--json", "run",
             "--stl", stl, "--rng", "--input", "not valid json"],
            capture_output=True, text=True, timeout=60,
        )
        assert r.returncode == 1
        lines = [l for l in r.stderr.splitlines() if l.strip()]
        assert any(_is_json_object(l) for l in lines), \
            f"no JSON record on stderr: {r.stderr!r}"


class TestServerCLI:
    """Launch the serve/rpc/backend CLI handlers (cmd_* + uvicorn) as subprocesses."""

    @staticmethod
    def _launch(args, ready_path, timeout=40):
        port = _free_port()
        proc = subprocess.Popen(
            [sys.executable, "-m", "autocog", *args,
             "--host", "127.0.0.1", "--port", str(port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        url = f"http://127.0.0.1:{port}{ready_path}"
        deadline = time.time() + timeout
        while time.time() < deadline:
            if proc.poll() is not None:
                proc.wait()
                raise AssertionError(f"server exited early (rc={proc.returncode})")
            try:
                with urllib.request.urlopen(url, timeout=1) as resp:
                    if resp.status == 200:
                        return port, proc
            except Exception:
                time.sleep(0.3)
        proc.terminate()
        raise AssertionError(f"server did not come up at {url}")

    @staticmethod
    def _stop(proc):
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)

    def test_backend_cli(self):
        port, proc = self._launch(["backend", "--rng"], "/models")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/models") as resp:
                models = json.loads(resp.read())
            assert "rng" in models["models"]
        finally:
            self._stop(proc)

    def test_rpc_cli(self, repo_root):
        stl = str(repo_root / "share/demos/mcq/select.stl")
        port, proc = self._launch(["rpc", "--stl", stl, "--rng"], "/schema")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/schema") as resp:
                schema = json.loads(resp.read())
            assert "main" in schema
        finally:
            self._stop(proc)

    def test_serve_cli(self, repo_root):
        stl = str(repo_root / "share/demos/mcq/select.stl")
        port, proc = self._launch(["serve", "--stl", stl, "--rng"], "/schema")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/schema") as resp:
                schema = json.loads(resp.read())
            assert "main" in schema
        finally:
            self._stop(proc)
