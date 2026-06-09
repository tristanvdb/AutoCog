"""
Smoke tests — verify bindings load and basic pipeline works.
"""

import autocog.errors
import pytest

import contextlib


@contextlib.contextmanager
def running_server(app):
    """Run a uvicorn app in a background thread on an ephemeral port.

    Yields the chosen port. This avoids the flaky fixed-port + fixed-sleep
    pattern: the OS assigns a free port (so tests never collide on a hardcoded
    one), startup is detected by polling the port rather than sleeping a fixed
    interval, and the server thread is joined on exit so the port is released
    before the next test tries to bind.
    """
    import socket
    import threading
    import time
    import uvicorn

    # Ask the OS for a free port, then hand it to uvicorn.
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    config = uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error")
    server = uvicorn.Server(config)
    thread = threading.Thread(target=server.run, daemon=True)
    thread.start()

    # Wait until the server is actually accepting connections.
    deadline = time.time() + 15.0
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                break
        except OSError:
            time.sleep(0.05)
    else:
        server.should_exit = True
        thread.join(timeout=5)
        raise RuntimeError(f"uvicorn did not start on port {port}")

    try:
        yield port
    finally:
        server.should_exit = True
        thread.join(timeout=5)


class TestBindings:
    """Verify all three bindings import and respond."""

    def test_compiler_import(self):
        from autocog.compiler.stl import compiler_stl_cxx
        assert hasattr(compiler_stl_cxx, "compile")

    def test_runtime_import(self):
        from autocog.runtime.sta import runtime_sta_cxx
        assert hasattr(runtime_sta_cxx, "instantiate")

    def test_backend_import(self):
        from autocog.backend.llama import backend_llama_cxx
        assert backend_llama_cxx.vocab_size(0) == 258  # RNG model


class TestCompile:
    """Verify compilation works."""

    def test_compile_select(self, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        assert "main" in prog.prompts
        assert "main" in prog.entry_points

    def test_emit_sta(self, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        sta = prog.sta
        assert "prompts" in sta
        assert "entry_points" in sta
        assert "main" in sta["prompts"]
        # Verify channels are present
        channels = sta["prompts"]["main"].get("channels", [])
        assert len(channels) > 0


class TestStoreDedup:
    """The datastore is content-addressed: identical artifacts collapse to one
    handle. That shared entry must outlive any single holder being released or
    garbage-collected — otherwise dropping one duplicate corrupts the others."""

    def test_dedup_survives_duplicate_release(self, repo_root):
        import gc
        import autocog

        src = str(repo_root / "share/demos/mcq/select.stl")
        # Two independent compiles of the same source are byte-identical, so
        # they resolve to the same content uid (one store entry, two holders).
        p1 = autocog.compile(src)
        p2 = autocog.compile(src)
        assert p1.id == p2.id, "identical programs must content-address to one handle"

        # Dropping one holder (its __del__ releases the uid) must NOT invalidate
        # the entry the surviving holder still points at.
        del p2
        gc.collect()

        sta = p1.sta  # faults if the shared entry was destroyed by p2's release
        assert "prompts" in sta


class TestPipeline:
    """Verify the full execution pipeline."""

    def test_select_with_inputs(self, engine, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        result = engine.run(
            prog,
            topic="Science",
            question="What is H2O?",
            choices=["Water", "Fire", "Air", "Earth"]
        )
        # RNG model picks a choice — result should be a string (single _ return)
        assert isinstance(result, str)
        assert result in ["Water", "Fire", "Air", "Earth"]

    def test_multi_prompt_flow(self, engine, repo_root):
        import autocog
        prog = autocog.compile(
            str(repo_root / "tests/fixtures/stl/flows/data/test_input_dataflow.stl")
        )
        result = engine.run(prog, name="Alice", age="25")
        # Returns a string (single _ return from summarizer)
        assert isinstance(result, str)
        assert len(result) > 0

    def test_call_channel(self, engine, repo_root):
        import autocog
        prog = autocog.compile(
            str(repo_root / "tests/fixtures/stl/flows/calls/test_call_basic.stl")
        )
        result = engine.run(prog, topic="science")
        assert isinstance(result, str)
        assert len(result) > 0


class TestRealModel:
    """Tests with real GGUF models. Skipped if models unavailable."""

    def test_llama3_engine(self, real_engine):
        """Verify Engine initializes with the llama3-test model."""
        assert real_engine.model_id > 0
        from autocog.backend.llama import backend_llama_cxx
        assert backend_llama_cxx.vocab_size(real_engine.model_id) == 128256

    def test_llama3_tokenize_roundtrip(self, real_engine):
        """Verify tokenization roundtrip with real tokenizer."""
        from autocog.backend.llama import backend_llama_cxx
        text = "topic:Science\nquestion:What is H2O?\n"
        tokens = backend_llama_cxx.tokenize(real_engine.model_id, text, False)
        assert len(tokens) > 0
        result = backend_llama_cxx.detokenize(real_engine.model_id, tokens)
        assert result == text

    def test_llama3_select(self, real_engine, repo_root):
        """Run select.stl with the real model — full evaluation."""
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        result = real_engine.run(
            prog,
            topic="Science",
            question="What is H2O?",
            choices=["Water", "Fire", "Air", "Earth"]
        )
        assert isinstance(result, str)
        assert result in ["Water", "Fire", "Air", "Earth"]

    def test_llama3_call_basic(self, real_engine, repo_root):
        """Run call_basic with the real model — sub-prompt execution."""
        import autocog
        prog = autocog.compile(
            str(repo_root / "tests/fixtures/stl/flows/calls/test_call_basic.stl")
        )
        result = real_engine.run(prog, topic="science")
        assert isinstance(result, str)
        assert len(result) > 0


class TestWriterDemo:
    """End-to-end test of the writer demo with RNG model."""

    def test_writer_completes(self, engine, repo_root):
        import autocog
        prog = autocog.compile(
            str(repo_root / "share/demos/story-writer/writer.stl"),
            includes=[
                str(repo_root / "share/library"),
                str(repo_root / "share/demos/story-writer"),
            ]
        )
        from autocog.__main__ import load_externals
        externals = load_externals(prog, [
            str(repo_root / "share/library"),
            str(repo_root / "share/demos/story-writer"),
        ])
        from autocog.context import Context
        ctx = Context(prog, engine, "init_idea",
                      {"query": "bedtime story", "age": "3"}, externals)
        steps = 0
        try:
            while not ctx.done and steps < 50:
                ctx.step()
                steps += 1
        except (RuntimeError, autocog.errors.AutoCogError):
            pass  # Writer with RNG may fail at any step



class TestSchema:
    """Test input/output schema in STA entry_points."""

    def test_mcq_schema(self, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        s = prog.input_schema("main")
        assert "topic" in s
        assert "question" in s
        assert "choices" in s
        assert s["topic"]["type"] == "text"
        assert s["topic"]["required"] is True
        assert s["choices"]["type"] == "array"
        assert s["choices"]["items"]["type"] == "text"
        assert s["choices"]["length"] == 4
        # Output schema
        o = prog.output_schema("main")
        assert "_" in o

    def test_writer_schema(self, repo_root):
        import autocog
        prog = autocog.compile(
            str(repo_root / "share/demos/story-writer/writer.stl"),
            includes=[str(repo_root / "share/demos/story-writer")]
        )
        s = prog.input_schema("main")
        assert "query" in s
        assert "age" in s
        assert s["query"]["required"] is True
        assert s["age"]["required"] is True
        assert "enum" in s["age"]
        assert len(s["age"]["enum"]) == 12
        # Output schema
        o = prog.output_schema("main")
        assert "done" in o

    def test_loaded_program_schema(self, repo_root):
        import autocog, json, tempfile, os
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump(prog.sta, f)
            tmp = f.name
        try:
            prog2 = autocog.load(tmp)
            s = prog2.input_schema("main")
            assert "topic" in s
        finally:
            os.unlink(tmp)


class TestStapp:
    """Test .stapp pack and run."""

    def test_pack_and_run(self, repo_root):
        import autocog, tempfile, os
        from autocog.stapp import pack, load_stapp
        import shutil

        stl = str(repo_root / "share/demos/mcq/select.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name

        try:
            pack(stl, [], stapp_path)
            prog, manifest, temp_dir, inc = load_stapp(stapp_path)
            try:
                assert "main" in prog.entry_points
                engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))
                result = engine.run(prog, externals={}, topic="Sci", question="2+2?", choices=["3","4","5","6"])
                assert isinstance(result, str)
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        finally:
            os.unlink(stapp_path)

    def test_pack_manifest_abi_version_populated(self, repo_root):
        """The .stapp manifest records a non-empty ABI version.

        Regression guard: the manifest abi_version must be populated and match
        the installed autocog version. An empty value silently disables the
        load-time ABI gate, so this asserts it is present and correct.
        """
        import autocog, tempfile, os, json, zipfile
        from autocog.stapp import pack

        stl = str(repo_root / "share/demos/mcq/select.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        try:
            pack(stl, [], stapp_path)
            with zipfile.ZipFile(stapp_path) as zf:
                manifest = json.loads(zf.read("manifest.json"))
            abi = manifest.get("abi_version")
            assert abi, f"manifest abi_version is empty/missing: {abi!r}"
            assert abi == autocog.__version__, (
                f"manifest abi_version {abi!r} != installed {autocog.__version__!r}"
            )
        finally:
            os.unlink(stapp_path)

    def test_pack_no_source(self, repo_root):
        import tempfile, os, zipfile
        from autocog.stapp import pack

        stl = str(repo_root / "share/demos/mcq/select.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name

        try:
            pack(stl, [], stapp_path, no_source=True)
            with zipfile.ZipFile(stapp_path) as zf:
                names = zf.namelist()
                assert "program.sta.json" in names
                assert "select.stl" not in names
        finally:
            os.unlink(stapp_path)

    def test_pack_recompile(self, repo_root):
        import autocog, tempfile, os
        from autocog.stapp import pack, load_stapp
        import shutil

        stl = str(repo_root / "share/demos/mcq/select.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name

        try:
            pack(stl, [], stapp_path)
            prog, manifest, temp_dir, inc = load_stapp(stapp_path, recompile=True)
            try:
                assert "main" in prog.entry_points
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        finally:
            os.unlink(stapp_path)

    def test_writer_stapp(self, repo_root):
        import autocog, tempfile, os
        from autocog.stapp import pack, load_stapp
        from autocog.__main__ import load_externals
        import shutil

        stl = str(repo_root / "share/demos/story-writer/writer.stl")
        inc = [str(repo_root / "share/demos/story-writer")]
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name

        try:
            pack(stl, inc, stapp_path)
            prog, manifest, temp_dir, inc_paths = load_stapp(stapp_path)
            try:
                engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))
                externals = load_externals(prog, inc_paths)
                from autocog.context import Context
                ctx = Context(prog, engine, prog.entry_prompt("main"),
                              {"query": "bedtime", "age": "3"}, externals)
                steps = 0
                try:
                    while not ctx.done and steps < 30:
                        ctx.step()
                        steps += 1
                except (RuntimeError, autocog.errors.AutoCogError):
                    pass  # Flow limit exceeded is expected with RNG
                pass  # Writer with RNG may fail at any step
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        finally:
            os.unlink(stapp_path)


class TestRemoteEngine:
    """Test RemoteEngine against in-process servers."""

    def test_remote_run_level1(self, repo_root):
        """Test remote_run against a serve endpoint."""
        import autocog
        from autocog.server.serve import create_app
        from autocog.__main__ import load_externals

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax, search_path=str(repo_root / "share/search/default.json"))
        with running_server(app) as port:
            result = autocog.remote_run(
                f"http://127.0.0.1:{port}",
                topic="Science", question="What is H2O?",
                choices=["Water", "Fire", "Air", "Earth"]
            )
            assert result in ["Water", "Fire", "Air", "Earth"]

    def test_remote_engine_level2(self, repo_root):
        """Test RemoteEngine against an RPC endpoint."""
        import autocog
        from autocog.server.rpc import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax, search_path=str(repo_root / "share/search/default.json"))
        with running_server(app) as port:
            engine = autocog.RemoteEngine(f"http://127.0.0.1:{port}")
            result = engine.run(
                prog,
                topic="Science", question="What is H2O?",
                choices=["Water", "Fire", "Air", "Earth"]
            )
            assert result in ["Water", "Fire", "Air", "Earth"]


class TestBackendServer:
    """Test the level-3 backend server."""

    def test_backend_evaluate(self, repo_root):
        """Submit FTA to backend, poll for result."""
        import autocog, json, urllib.request
        from autocog.runtime.sta import runtime_sta_cxx
        from autocog.server.backend import create_app

        # Compile and instantiate to an FTA, then fetch it as JSON straight from
        # the datastore (dump_fta) -- no need to shell out to the ista tool.
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        search = str(repo_root / "share/search/default.json")
        engine = autocog.Engine(syntax=syntax, search=search)
        content = {"topic": "Science", "question": "2+2?", "choices": ["3", "4", "5", "6"]}
        fta_id = runtime_sta_cxx.instantiate(
            prog.id, "main", content, engine.syntax_id, engine.search_id
        )
        try:
            fta_json = json.loads(runtime_sta_cxx.dump_fta(fta_id))
        finally:
            runtime_sta_cxx.release_fta(fta_id)

        # Start backend server
        app = create_app(model_path=None)  # RNG model
        with running_server(app) as port:
            # Test GET /models
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/models") as resp:
                models = json.loads(resp.read())
            assert "rng" in models["models"]

            # Test POST /evaluate
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}/evaluate",
                data=json.dumps({"fta": fta_json}).encode(),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urllib.request.urlopen(req) as resp:
                submit = json.loads(resp.read())
            request_id = submit["request_id"]

            # Poll
            import time
            for _ in range(30):
                time.sleep(0.5)
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/status/{request_id}") as resp:
                    status = json.loads(resp.read())
                if status["state"] == "complete":
                    # Backend returns the FTT (xfta over the wire), not text.
                    ftt = status["result"]
                    assert isinstance(ftt, dict)
                    assert "children" in ftt and "text" in ftt
                    return
                elif status["state"] == "error":
                    raise AssertionError(f"Backend error: {status['error']}")
            raise AssertionError("Backend evaluation timed out")

    def test_remote_backend_level3(self, repo_root):
        """RemoteBackend instantiates+walks locally, evaluates on the backend."""
        import autocog
        from autocog.server.backend import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        app = create_app(model_path=None)  # RNG model
        with running_server(app) as port:
            engine = autocog.RemoteBackend(
                f"http://127.0.0.1:{port}",
                syntax=str(repo_root / "share/syntax/default.json"),
                search=str(repo_root / "share/search/default.json"),
            )
            result = engine.run(
                prog,
                topic="Science", question="What is H2O?",
                choices=["Water", "Fire", "Air", "Earth"],
            )
            assert result in ["Water", "Fire", "Air", "Earth"]


class TestServeServer:
    """Test the level-1 serve server including web form."""

    def test_web_form(self, repo_root):
        """Test GET / returns HTML form."""
        import autocog, urllib.request
        from autocog.server.serve import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax, search_path=str(repo_root / "share/search/default.json"))
        with running_server(app) as port:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/") as resp:
                html = resp.read().decode()
            assert "<form" in html
            assert "topic" in html
            assert "question" in html
            assert "choices" in html

    def test_schema_endpoint(self, repo_root):
        """Test GET /schema returns entry point schemas."""
        import autocog, json, urllib.request
        from autocog.server.serve import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax, search_path=str(repo_root / "share/search/default.json"))
        with running_server(app) as port:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/schema") as resp:
                schema = json.loads(resp.read())
            assert "main" in schema
            assert "inputs" in schema["main"]
            assert "topic" in schema["main"]["inputs"]


class TestCLI:
    """Test CLI subcommands via subprocess."""

    def test_compile_stdout(self, repo_root):
        import subprocess, json
        result = subprocess.run(
            ["autocog", "compile", "--stl", str(repo_root / "share/demos/mcq/select.stl")],
            capture_output=True, text=True, timeout=30
        )
        assert result.returncode == 0
        sta = json.loads(result.stdout)
        assert "entry_points" in sta
        assert "prompts" in sta

    def test_compile_to_file(self, repo_root):
        import subprocess, json, tempfile, os
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outpath = f.name
        try:
            result = subprocess.run(
                ["autocog", "compile", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                 "-o", outpath],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            with open(outpath) as f:
                sta = json.load(f)
            assert "main" in sta["entry_points"]
        finally:
            os.unlink(outpath)

    def test_run_stl_rng(self, repo_root):
        import subprocess
        result = subprocess.run(
            ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
             "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}'],
            capture_output=True, text=True, timeout=30
        )
        assert result.returncode == 0
        assert result.stdout.strip() in ["3", "4", "5", "6"]

    def test_run_sta_rng(self, repo_root):
        import subprocess, json, tempfile, os
        # Compile first
        compile_result = subprocess.run(
            ["autocog", "compile", "--stl", str(repo_root / "share/demos/mcq/select.stl")],
            capture_output=True, text=True, timeout=30
        )
        with tempfile.NamedTemporaryFile(mode='w', suffix=".json", delete=False) as f:
            f.write(compile_result.stdout)
            sta_path = f.name
        try:
            result = subprocess.run(
                ["autocog", "run", "--sta", sta_path,
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}'],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            assert result.stdout.strip() in ["3", "4", "5", "6"]
        finally:
            os.unlink(sta_path)

    def test_run_input_file(self, repo_root):
        import subprocess, json, tempfile, os
        with tempfile.NamedTemporaryFile(mode='w', suffix=".json", delete=False) as f:
            json.dump({"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5", "6"]}, f)
            input_path = f.name
        try:
            result = subprocess.run(
                ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                 "--rng", "--input", input_path],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
        finally:
            os.unlink(input_path)

    def test_run_verbose(self, repo_root):
        import subprocess
        result = subprocess.run(
            ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
             "--rng", "-v", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}'],
            capture_output=True, text=True, timeout=30
        )
        assert result.returncode == 0
        assert "Done in" in result.stderr or "Done in" in result.stdout

    def test_run_output_file(self, repo_root):
        import subprocess, tempfile, os
        with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as f:
            outpath = f.name
        try:
            result = subprocess.run(
                ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}',
                 "-o", outpath],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            with open(outpath) as f:
                assert f.read().strip() in ["3", "4", "5", "6"]
        finally:
            os.unlink(outpath)

    def test_pack_and_run_app(self, repo_root):
        import subprocess, tempfile, os
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        try:
            # Pack
            result = subprocess.run(
                ["autocog", "pack", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                 "-o", stapp_path],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0

            # Run from .stapp
            result = subprocess.run(
                ["autocog", "run", "--app", stapp_path,
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}'],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            assert result.stdout.strip() in ["3", "4", "5", "6"]
        finally:
            os.unlink(stapp_path)


class TestCLIFunctions:
    """Test CLI command functions directly (for coverage)."""

    def test_cmd_compile(self, repo_root):
        import sys, io, json
        from unittest.mock import patch
        from autocog.__main__ import main

        args = ["autocog", "compile", "--stl", str(repo_root / "share/demos/mcq/select.stl")]
        with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
            main()
        sta = json.loads(mock_out.getvalue())
        assert "entry_points" in sta

    def test_cmd_compile_to_file(self, repo_root):
        import tempfile, os, json
        from unittest.mock import patch
        from autocog.__main__ import main

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            outpath = f.name
        try:
            args = ["autocog", "compile", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                    "-o", outpath]
            with patch("sys.argv", args):
                main()
            with open(outpath) as f:
                sta = json.load(f)
            assert "main" in sta["entry_points"]
        finally:
            os.unlink(outpath)

    def test_cmd_run(self, repo_root):
        import sys, io, json
        from unittest.mock import patch
        from autocog.__main__ import main

        args = ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}']
        with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
            main()
        assert mock_out.getvalue().strip() in ["3", "4", "5", "6"]

    def test_cmd_run_verbose(self, repo_root):
        import sys, io
        from unittest.mock import patch
        from autocog.__main__ import main

        args = ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                "--rng", "-v", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}']
        with patch("sys.argv", args), \
             patch("sys.stdout", new_callable=io.StringIO), \
             patch("sys.stderr", new_callable=io.StringIO) as mock_err:
            main()
        assert "Done in" in mock_err.getvalue()

    def test_cmd_run_app(self, repo_root):
        import sys, io, tempfile, os
        from unittest.mock import patch
        from autocog.__main__ import main

        # Pack first
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        args = ["autocog", "pack", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                "-o", stapp_path]
        with patch("sys.argv", args):
            main()

        try:
            # Run from .stapp
            args = ["autocog", "run", "--app", stapp_path,
                    "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}']
            with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
                main()
            assert mock_out.getvalue().strip() in ["3", "4", "5", "6"]
        finally:
            os.unlink(stapp_path)

    def test_cmd_pack(self, repo_root):
        import sys, tempfile, os, zipfile
        from unittest.mock import patch
        from autocog.__main__ import main

        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        try:
            args = ["autocog", "pack", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                    "-o", stapp_path]
            with patch("sys.argv", args):
                main()
            with zipfile.ZipFile(stapp_path) as zf:
                assert "manifest.json" in zf.namelist()
                assert "program.sta.json" in zf.namelist()
        finally:
            os.unlink(stapp_path)

    def test_cmd_run_input_file(self, repo_root):
        import sys, io, json, tempfile, os
        from unittest.mock import patch
        from autocog.__main__ import main

        with tempfile.NamedTemporaryFile(mode='w', suffix=".json", delete=False) as f:
            json.dump({"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5", "6"]}, f)
            input_path = f.name
        try:
            args = ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                    "--rng", "--input", input_path]
            with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
                main()
            assert mock_out.getvalue().strip() in ["3", "4", "5", "6"]
        finally:
            os.unlink(input_path)


class TestCoverageEdgeCases:
    """Targeted tests for uncovered branches."""

    def test_stapp_vendor_stdlib(self, repo_root):
        """Pack with --vendor-stdlib bundles stlib files."""
        import tempfile, os, zipfile
        from autocog.stapp import pack
        stl = str(repo_root / "share/demos/mcq/select-iter.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        try:
            pack(stl, [], stapp_path, vendor_stdlib=True)
            with zipfile.ZipFile(stapp_path) as zf:
                names = zf.namelist()
                assert any(n.startswith("stlib/") for n in names), f"No stlib in {names}"
                assert "stlib/thoughts.stl" in names
        finally:
            os.unlink(stapp_path)

    def test_stapp_no_compile(self, repo_root):
        """Pack with --no-compile omits STA."""
        import tempfile, os, zipfile
        from autocog.stapp import pack
        stl = str(repo_root / "share/demos/mcq/select.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        try:
            pack(stl, [], stapp_path, no_compile=True)
            with zipfile.ZipFile(stapp_path) as zf:
                names = zf.namelist()
                assert "program.sta.json" not in names
                assert "select.stl" in names
        finally:
            os.unlink(stapp_path)

    def test_stapp_load_source_only(self, repo_root):
        """Load a .stapp with no STA (source only) — must compile."""
        import autocog, tempfile, os, shutil
        from autocog.stapp import pack, load_stapp
        stl = str(repo_root / "share/demos/mcq/select.stl")
        with tempfile.NamedTemporaryFile(suffix=".stapp", delete=False) as f:
            stapp_path = f.name
        try:
            pack(stl, [], stapp_path, no_compile=True)
            prog, manifest, temp_dir, inc = load_stapp(stapp_path)
            try:
                assert "main" in prog.entry_points
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        finally:
            os.unlink(stapp_path)

    def test_context_done_noop(self, repo_root):
        """Calling step() on a completed context is a no-op."""
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))
        ctx = autocog.Context(prog, engine, "main",
                              {"topic": "X", "question": "Y", "choices": ["a", "b", "c", "d"]})
        while not ctx.done:
            ctx.step()
        # Call again — should be no-op
        ctx.step()
        assert ctx.done

    def test_context_flow_conditional(self, repo_root):
        """Test a program with conditional flow (writer has loops).
        With RNG model, flow decisions are random — may hit flow limits.
        The test exercises the flow resolution code path regardless."""
        import autocog
        from autocog.__main__ import load_externals
        prog = autocog.compile(
            str(repo_root / "share/demos/story-writer/writer.stl"),
            includes=[str(repo_root / "share/demos/story-writer")]
        )
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))
        externals = load_externals(prog, [str(repo_root / "share/demos/story-writer")])
        ctx = autocog.Context(prog, engine, prog.entry_prompt("main"),
                              {"query": "test", "age": "3"}, externals)
        steps = 0
        try:
            while not ctx.done and steps < 30:
                ctx.step()
                steps += 1
        except (RuntimeError, autocog.errors.AutoCogError):
            pass  # Flow limit exceeded is expected with RNG
        pass  # Writer with RNG may fail at any step

    def test_engine_run_async(self, repo_root):
        """Test async run path."""
        import asyncio, autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))
        result = asyncio.run(engine.run_async(
            prog, topic="Sci", question="2+2?", choices=["3", "4", "5", "6"]
        ))
        assert result in ["3", "4", "5", "6"]

    def test_remote_error_handling(self, repo_root):
        """Test RemoteEngine error path."""
        import autocog, json, urllib.request
        from autocog.server.rpc import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax, search_path=str(repo_root / "share/search/default.json"))
        with running_server(app) as port:
            # Request with bad prompt name should cause error
            engine = autocog.RemoteEngine(f"http://127.0.0.1:{port}", poll_interval=0.2, timeout=5)
            try:
                engine.evaluate_prompt(prog, "nonexistent_prompt", {})
                assert False, "Should have raised"
            except (RuntimeError, autocog.errors.AutoCogError) as e:
                assert "failed" in str(e).lower() or "error" in str(e).lower()

    def test_status_not_found(self, repo_root):
        """Test GET /status with unknown request_id returns 404."""
        import autocog, urllib.request, urllib.error, json
        from autocog.server.serve import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax, search_path=str(repo_root / "share/search/default.json"))
        with running_server(app) as port:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/status/nonexistent-id")
                assert False, "Should have raised"
            except urllib.error.HTTPError as e:
                assert e.code == 404


class TestVersionAndBuildInfo:
    """Test --version and --build-info CLI options."""

    def test_version(self):
        import subprocess
        result = subprocess.run(
            ["autocog", "--version"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 0
        assert "autocog" in result.stdout

    def test_build_info(self):
        import subprocess
        result = subprocess.run(
            ["autocog", "--build-info"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 0
        assert "build_type" in result.stdout
        assert "compiler" in result.stdout

    def test_version_direct(self):
        import autocog
        assert hasattr(autocog, "__version__")
        assert len(autocog.__version__) > 0


class TestRecorder:
    """Test execution recording."""

    def test_record_frame(self, repo_root):
        import autocog, tempfile, os, json
        from autocog.recorder import Recorder

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))

        with tempfile.TemporaryDirectory(prefix="autocog-test-") as tmpdir:
            recorder = Recorder(kinds="frame", path=tmpdir)
            result = engine.run(
                prog, recorder=recorder,
                topic="Sci", question="2+2?", choices=["3", "4", "5", "6"]
            )

            # Check context trace
            ctx_path = os.path.join(tmpdir, "ctx-0.json")
            assert os.path.isfile(ctx_path)
            with open(ctx_path) as f:
                ctx = json.load(f)
            assert ctx["entry"] == "main"
            assert len(ctx["trace"]) > 0

            # Each artifact is its own typed file.
            prompt = ctx["trace"][0]["prompt"]
            step = ctx["trace"][0]["step"]
            frame_path = os.path.join(tmpdir, f"ctx-0/{prompt}/{step}.frame.json")
            assert os.path.isfile(frame_path)
            with open(frame_path) as f:
                frame = json.load(f)
            assert isinstance(frame, dict)

    def test_record_input(self, repo_root):
        import autocog, tempfile, os, json
        from autocog.recorder import Recorder

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))

        with tempfile.TemporaryDirectory(prefix="autocog-test-") as tmpdir:
            recorder = Recorder(kinds="input", path=tmpdir)
            result = engine.run(
                prog, recorder=recorder,
                topic="Sci", question="2+2?", choices=["3", "4", "5", "6"]
            )

            ctx_path = os.path.join(tmpdir, "ctx-0.json")
            with open(ctx_path) as f:
                ctx = json.load(f)
            prompt = ctx["trace"][0]["prompt"]
            step = ctx["trace"][0]["step"]
            input_path = os.path.join(tmpdir, f"ctx-0/{prompt}/{step}.input.json")
            assert os.path.isfile(input_path)
            with open(input_path) as f:
                data = json.load(f)
            assert "topic" in data
            # frame was not requested -> no frame file
            assert not os.path.isfile(os.path.join(tmpdir, f"ctx-0/{prompt}/{step}.frame.json"))

    def test_record_invalid_kind(self):
        from autocog.recorder import Recorder
        import pytest
        with pytest.raises(autocog.errors.ConfigError, match="Invalid record kinds"):
            Recorder(kinds="frame,bogus")

    def test_record_cli(self, repo_root):
        import subprocess, tempfile, os, json
        with tempfile.TemporaryDirectory(prefix="autocog-test-") as tmpdir:
            result = subprocess.run(
                ["autocog", "run",
                 "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5","6"]}',
                 "--record", "frame",
                 "--record-path", tmpdir],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            assert os.path.isfile(os.path.join(tmpdir, "ctx-0.json"))


class TestSyntaxVariants:
    """Test different syntax files."""

    def test_plain_syntax(self, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/plain.json"), search=str(repo_root / "share/search/default.json"))
        result = engine.run(prog, topic="Sci", question="2+2?", choices=["3", "4", "5", "6"])
        assert result in ["3", "4", "5", "6"]

    def test_chatml_syntax(self, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/chatml.json"), search=str(repo_root / "share/search/default.json"))
        result = engine.run(prog, topic="Sci", question="2+2?", choices=["3", "4", "5", "6"])
        assert result in ["3", "4", "5", "6"]

    def test_llama2chat_syntax(self, repo_root):
        import autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/llama2chat.json"), search=str(repo_root / "share/search/default.json"))
        result = engine.run(prog, topic="Sci", question="2+2?", choices=["3", "4", "5", "6"])
        assert result in ["3", "4", "5", "6"]

    def test_missing_syntax_field(self, repo_root):
        import autocog, autocog.errors, tempfile, json, os
        # Write a syntax file missing a required field
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump({"system_msg": "test"}, f)
            bad_syntax = f.name
        try:
            with __import__("pytest").raises(ValueError, match="malformed Syntax"):
                autocog.Engine(syntax=bad_syntax, search=str(repo_root / "share/search/default.json"))
        finally:
            os.unlink(bad_syntax)


class TestRecorderFtaFtt:
    """Test recording FTA and FTT artifacts."""

    def test_record_all(self, repo_root):
        import autocog, tempfile, os, json
        from autocog.recorder import Recorder

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"), search=str(repo_root / "share/search/default.json"))

        with tempfile.TemporaryDirectory(prefix="autocog-test-") as tmpdir:
            recorder = Recorder(kinds="input,frame,fta,ftt", path=tmpdir)
            result = engine.run(
                prog, recorder=recorder,
                topic="Sci", question="2+2?", choices=["3", "4", "5", "6"]
            )

            ctx_path = os.path.join(tmpdir, "ctx-0.json")
            assert os.path.isfile(ctx_path)
            with open(ctx_path) as f:
                ctx = json.load(f)

            prompt = ctx["trace"][0]["prompt"]
            step = ctx["trace"][0]["step"]
            base = os.path.join(tmpdir, f"ctx-0/{prompt}")

            # Each artifact is its own typed file.
            def load(kind):
                p = os.path.join(base, f"{step}.{kind}.json")
                assert os.path.isfile(p), f"missing {p}"
                with open(p) as f:
                    return json.load(f)

            input_data = load("input")
            frame_data = load("frame")
            fta_data = load("fta")
            ftt_data = load("ftt")

            assert "topic" in input_data
            assert isinstance(frame_data, dict)
            # FTA should have actions
            assert "actions" in fta_data
            # FTT should have tree structure
            assert "children" in ftt_data
            assert "text" in ftt_data


class TestCLIErrors:
    """Test CLI error handling for missing files and bad arguments."""

    def test_missing_stl_file(self):
        import subprocess
        result = subprocess.run(
            ["autocog", "compile", "--stl", "/nonexistent/program.stl"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "not found" in result.stderr.lower()

    def test_missing_sta_file(self):
        import subprocess
        result = subprocess.run(
            ["autocog", "run", "--sta", "/nonexistent/program.sta.json",
             "--rng", "--input", "{}"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "not found" in result.stderr.lower()

    def test_missing_app_file(self):
        import subprocess
        result = subprocess.run(
            ["autocog", "run", "--app", "/nonexistent/app.stapp",
             "--rng", "--input", "{}"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "not found" in result.stderr.lower()

    def test_missing_model_file(self, repo_root):
        import subprocess
        result = subprocess.run(
            ["autocog", "run",
             "--stl", str(repo_root / "share/demos/mcq/select.stl"),
             "--model", "/nonexistent/model.gguf",
             "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}'],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "not found" in result.stderr.lower()

    def test_missing_syntax_file(self, repo_root):
        import subprocess
        result = subprocess.run(
            ["autocog", "run",
             "--stl", str(repo_root / "share/demos/mcq/select.stl"),
             "--rng", "--syntax", "/nonexistent/syntax.json",
             "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}'],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "not found" in result.stderr.lower()

    def test_invalid_input_json(self, repo_root):
        import subprocess
        result = subprocess.run(
            ["autocog", "run",
             "--stl", str(repo_root / "share/demos/mcq/select.stl"),
             "--rng", "--input", "not valid json"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "json" in result.stderr.lower() or "error" in result.stderr.lower()

    def test_missing_pack_stl(self):
        import subprocess
        result = subprocess.run(
            ["autocog", "pack", "--stl", "/nonexistent/program.stl", "-o", "/tmp/test.stapp"],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "not found" in result.stderr.lower()

    def test_no_model_or_rng(self, repo_root):
        import subprocess
        result = subprocess.run(
            ["autocog", "run",
             "--stl", str(repo_root / "share/demos/mcq/select.stl"),
             "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}'],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "model" in result.stderr.lower() or "rng" in result.stderr.lower()


class TestCLIErrorsDirect:
    """Test error handling via direct main() calls for coverage."""

    def test_missing_stl_direct(self):
        from unittest.mock import patch
        from autocog.__main__ import main
        args = ["autocog", "compile", "--stl", "/nonexistent/program.stl"]
        with patch("sys.argv", args):
            with __import__("pytest").raises(SystemExit) as exc:
                main()
            assert exc.value.code == 1

    def test_missing_model_direct(self, repo_root):
        from unittest.mock import patch
        from autocog.__main__ import main
        args = ["autocog", "run",
                "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                "--model", "/nonexistent/model.gguf",
                "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}']
        with patch("sys.argv", args):
            with __import__("pytest").raises(SystemExit) as exc:
                main()
            assert exc.value.code == 1

    def test_run_sta_direct(self, repo_root):
        """Test --sta path (line 79)."""
        import io, json, tempfile, os
        from unittest.mock import patch
        from autocog.__main__ import main
        import autocog

        # Compile to STA first
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            json.dump(prog.sta, f)
            sta_path = f.name
        try:
            args = ["autocog", "run", "--sta", sta_path, "--rng",
                    "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}']
            with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as out:
                main()
            assert out.getvalue().strip() in ["a", "b", "c", "d"]
        finally:
            os.unlink(sta_path)

    def test_run_with_record_direct(self, repo_root):
        """Test --record path (lines 218-220, 242)."""
        import io, tempfile
        from unittest.mock import patch
        from autocog.__main__ import main

        with tempfile.TemporaryDirectory() as tmpdir:
            args = ["autocog", "run",
                    "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                    "--rng",
                    "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}',
                    "--record", "frame",
                    "--record-path", tmpdir]
            with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO):
                main()
            import os
            assert os.path.isfile(os.path.join(tmpdir, "ctx-0.json"))

    def test_run_output_file_direct(self, repo_root):
        """Test -o output path (lines 256-257)."""
        import tempfile, os
        from unittest.mock import patch
        from autocog.__main__ import main

        with tempfile.NamedTemporaryFile(suffix='.txt', delete=False) as f:
            outpath = f.name
        try:
            args = ["autocog", "run",
                    "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                    "--rng",
                    "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}',
                    "-o", outpath]
            with patch("sys.argv", args):
                main()
            with open(outpath) as f:
                assert f.read().strip() in ["a", "b", "c", "d"]
        finally:
            os.unlink(outpath)

    def test_run_with_model_direct(self, repo_root):
        """Test --model path (line 205) — uses RNG model path."""
        # This tests the model loading path with a non-existent file
        # to verify the error message
        from unittest.mock import patch
        from autocog.__main__ import main
        args = ["autocog", "run",
                "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                "--model", "/nonexistent/m.gguf",
                "--input", '{"topic":"X","question":"Y","choices":["a","b","c","d"]}']
        with patch("sys.argv", args):
            with __import__("pytest").raises(SystemExit) as exc:
                main()
            assert exc.value.code == 1


class TestLogging:
    """Test unified logging (C++ ↔ Python bridge)."""

    def test_set_log_level(self):
        """set_log_level changes C++ and Python logger levels."""
        import logging
        import autocog

        logger = logging.getLogger("autocog")
        autocog.set_log_level(logging.DEBUG)
        assert logger.level == logging.DEBUG

        autocog.set_log_level(logging.WARNING)
        assert logger.level == logging.WARNING

    def test_bridge_sink_receives_info(self, repo_root):
        """C++ INFO records arrive on the Python autocog logger."""
        import logging
        import autocog

        # Capture log output
        records = []
        handler = logging.Handler()
        handler.emit = lambda record: records.append(record)
        logger = logging.getLogger("autocog")
        logger.addHandler(handler)
        old_level = logger.level

        try:
            autocog.set_log_level(logging.INFO)
            # Compile triggers INFO: "STA generated"
            autocog.compile(
                str(repo_root / "tests/fixtures/stl/language/structures/test_basic_record.stl")
            )
            info_records = [r for r in records if r.levelno >= logging.INFO]
            assert len(info_records) > 0, "Expected C++ INFO records via bridge"
            assert any("STA generated" in r.getMessage() for r in info_records)
        finally:
            logger.removeHandler(handler)
            autocog.set_log_level(old_level if old_level else logging.WARNING)

    def test_bridge_sink_filtering(self, repo_root):
        """C++ records below the set level are filtered out."""
        import logging
        import autocog

        records = []
        handler = logging.Handler()
        handler.emit = lambda record: records.append(record)
        logger = logging.getLogger("autocog")
        logger.addHandler(handler)

        try:
            autocog.set_log_level(logging.ERROR)
            autocog.compile(
                str(repo_root / "tests/fixtures/stl/language/structures/test_basic_record.stl")
            )
            # At ERROR level, no INFO/DEBUG/TRACE records should arrive
            below_error = [r for r in records if r.levelno < logging.ERROR]
            assert len(below_error) == 0, f"Expected no records below ERROR, got {len(below_error)}"
        finally:
            logger.removeHandler(handler)
            autocog.set_log_level(logging.WARNING)

    def test_trace_level_registered(self):
        """TRACE level (5) is registered in Python logging."""
        import logging
        import autocog

        assert autocog.TRACE == 5
        assert logging.getLevelName(5) == "TRACE"
        logger = logging.getLogger("autocog.test")
        assert hasattr(logger, "trace")


class TestErrorHierarchy:
    """Test typed exceptions across C++/Python boundary."""

    def test_file_error_missing_syntax(self):
        """Loading a nonexistent syntax file raises FileError with path."""
        from autocog.errors import FileError, AutoCogError
        from autocog.runtime.sta import runtime_sta_cxx

        with pytest.raises(FileError) as exc_info:
            runtime_sta_cxx.load_syntax("/tmp/nonexistent_syntax.json")
        e = exc_info.value
        assert "/tmp/nonexistent_syntax.json" in e.path
        assert isinstance(e, AutoCogError)

    def test_file_error_missing_search(self):
        """Loading a nonexistent search config raises FileError."""
        from autocog.errors import FileError
        from autocog.runtime.sta import runtime_sta_cxx

        with pytest.raises(FileError):
            runtime_sta_cxx.load_search("/tmp/nonexistent_search.json")

    def test_config_error_missing_field(self, repo_root):
        """Syntax file missing a required field is rejected.

        Until schema validation lands, the codec reports the malformed input as
        a ValueError (nlohmann access failure); a schema check will eventually
        reject it earlier with a typed ConfigError.
        """
        import autocog, tempfile, json, os

        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump({"system_msg": "test"}, f)
            bad_syntax = f.name
        try:
            with pytest.raises(ValueError, match="malformed Syntax"):
                autocog.Engine(syntax=bad_syntax,
                               search=str(repo_root / "share/search/default.json"))
        finally:
            os.unlink(bad_syntax)

    def test_compile_error(self, repo_root):
        """Compiling malformed STL raises CompileError with diagnostics."""
        import autocog
        from autocog.errors import CompileError, AutoCogError

        bad = repo_root / "tests/fixtures/stl/errors/test_missing_semicolon.stl"
        with pytest.raises(CompileError) as exc_info:
            autocog.compile(str(bad))
        e = exc_info.value
        assert isinstance(e, AutoCogError)
        assert e.diagnostics
        assert any(d.level == "error" for d in e.diagnostics)

    def test_config_error_bad_entry(self, repo_root):
        """Running with invalid entry point raises ConfigError."""
        import autocog
        from autocog.errors import ConfigError

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        with pytest.raises(ConfigError, match="not found"):
            prog.entry_prompt("nonexistent")

    def test_file_error_bad_program(self):
        """Loading nonexistent program file raises FileError."""
        from autocog.errors import FileError
        from autocog.runtime.sta import runtime_sta_cxx

        with pytest.raises(FileError) as exc_info:
            runtime_sta_cxx.load_sta("/tmp/nonexistent_program.json")
        e = exc_info.value
        assert "/tmp/nonexistent_program.json" in e.path
        assert isinstance(e, OSError)  # MI with builtins.OSError

    def test_error_hierarchy_relationships(self):
        """Error types have correct inheritance."""
        from autocog.errors import (
            AutoCogError, CompileError, ParseError, ExecutionError,
            ConfigError, ModelError, OrchestrationError, FlowInvariantError,
            RemoteError, Timeout, FileError, NotImplementedError,
            InternalError
        )
        import builtins

        # Compile tree
        assert issubclass(CompileError, AutoCogError)
        assert issubclass(ParseError, CompileError)

        # Execution tree
        assert issubclass(ExecutionError, AutoCogError)
        assert issubclass(ConfigError, ExecutionError)
        assert issubclass(ModelError, ExecutionError)
        assert issubclass(OrchestrationError, ExecutionError)
        assert issubclass(FlowInvariantError, OrchestrationError)
        assert issubclass(RemoteError, ExecutionError)
        assert issubclass(Timeout, ExecutionError)
        assert issubclass(FileError, ExecutionError)

        # MI with builtins
        assert issubclass(Timeout, builtins.TimeoutError)
        assert issubclass(FileError, builtins.OSError)
        assert issubclass(NotImplementedError, builtins.NotImplementedError)

        # InternalError
        assert issubclass(InternalError, AutoCogError)

    def test_handle_helper(self):
        """handle() returns correct exit codes."""
        import logging
        from autocog.errors import handle, ConfigError, InternalError

        log = logging.getLogger("autocog.test.handle")

        assert handle(ConfigError("bad"), log) == 1
        assert handle(InternalError("bug"), log) == 250
        assert handle(ValueError("other"), log) == 251

    def test_error_fields(self):
        """Error subtypes expose their typed fields."""
        from autocog.errors import (
            ConfigError, ModelError, OrchestrationError, RemoteError,
            Timeout, FileError
        )

        e = ConfigError("msg", path="/tmp/x")
        assert e.path == "/tmp/x"

        e = ModelError("msg", model_id=3, op="load")
        assert e.model_id == 3
        assert e.op == "load"

        e = OrchestrationError("msg", prompt="p", field="f")
        assert e.prompt == "p"
        assert e.field == "f"

        e = RemoteError("msg", endpoint="http://x")
        assert e.endpoint == "http://x"

        e = Timeout("msg", seconds=30.0)
        assert e.seconds == 30.0

        e = FileError("msg", path="/tmp/y")
        assert e.path == "/tmp/y"

    def test_except_builtins(self):
        """MI types are catchable by builtin except clauses."""
        from autocog.errors import Timeout, FileError, NotImplementedError
        import builtins

        try:
            raise Timeout("timed out", seconds=5.0)
        except builtins.TimeoutError:
            pass  # Should be caught

        try:
            raise FileError("not found", path="/x")
        except OSError:
            pass  # Should be caught

        try:
            raise NotImplementedError("todo")
        except builtins.NotImplementedError:
            pass  # Should be caught


class TestSchemaValidation:
    """Test JSON Schema validation of artifacts."""

    def test_validate_sta_artifact(self, repo_root):
        """validate_artifact passes on a valid STA."""
        import autocog
        from autocog._schema import validate_artifact
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        # Should not raise
        validate_artifact(prog.sta, filepath="test_mcq")

    def test_validate_detects_format(self, repo_root):
        """validate_artifact auto-detects STA format."""
        import autocog
        from autocog._schema import _detect_format
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        assert _detect_format(prog.sta) == "sta"

    def test_validate_invalid_artifact(self, repo_root):
        """validate_artifact raises ConfigError on invalid data."""
        import pytest
        from autocog._schema import validate_artifact
        from autocog.errors import ConfigError
        bad = {"metadata": {"format": "sta"}, "prompts": "not_an_object"}
        with pytest.raises(ConfigError, match="Schema violation"):
            validate_artifact(bad)


class TestGoldenCompare:
    """Test the golden file comparator."""

    def test_compare_identical(self, repo_root):
        """compare returns None when artifacts differ only by timestamp."""
        import sys
        sys.path.insert(0, str(repo_root / "tests" / "scripts"))
        from golden_compare import compare
        a = {"prompts": {"x": 1}, "metadata": {"version": "1.0.0", "timestamp": "2026-01-01T00:00:00Z"}}
        b = {"prompts": {"x": 1}, "metadata": {"version": "1.0.0", "timestamp": "2026-12-31T00:00:00Z"}}
        assert compare(a, b) is None  # only the timestamp differs, which is excluded

    def test_compare_mismatch(self, repo_root):
        """compare returns diff message on mismatch."""
        import sys
        sys.path.insert(0, str(repo_root / "tests" / "scripts"))
        from golden_compare import compare
        a = {"prompts": {"x": 1}}
        b = {"prompts": {"x": 2}}
        diff = compare(a, b)
        assert diff is not None
        assert "GOLDEN MISMATCH" in diff
        assert "$.prompts.x" in diff

    def test_compare_ignores_only_timestamp(self, repo_root):
        """compare excludes metadata.timestamp but compares the rest of metadata."""
        import sys
        sys.path.insert(0, str(repo_root / "tests" / "scripts"))
        from golden_compare import compare
        # Differing only in timestamp is treated as a match.
        a = {"data": 1, "metadata": {"uid": "aaaa", "timestamp": "2026-01-01T00:00:00Z"}}
        b = {"data": 1, "metadata": {"uid": "aaaa", "timestamp": "2026-12-31T00:00:00Z"}}
        assert compare(a, b) is None
        # A differing uid is a real mismatch now: identity is part of the comparison.
        c = {"data": 1, "metadata": {"uid": "bbbb", "timestamp": "2026-01-01T00:00:00Z"}}
        diff = compare(a, c)
        assert diff is not None
        assert "$.metadata.uid" in diff

    def test_validate_valid_golden(self, repo_root):
        """validate passes on a real golden file."""
        import json, sys
        sys.path.insert(0, str(repo_root / "tests" / "scripts"))
        from golden_compare import validate
        golden = json.load(open(
            repo_root / "tests/integration/tools/stlc/sta/language/structures/test_basic_record.golden.json"
        ))
        assert validate(golden) is None

    def test_validate_invalid_golden(self, repo_root):
        """validate returns error message on schema violation."""
        import sys
        sys.path.insert(0, str(repo_root / "tests" / "scripts"))
        from golden_compare import validate
        bad = {"metadata": {"format": "sta"}, "entry_points": 42}
        err = validate(bad)
        assert err is not None
        assert "SCHEMA VIOLATION" in err

    def test_format_diff_readability(self, repo_root):
        """format_diff produces JSONPath-anchored output."""
        import sys
        sys.path.insert(0, str(repo_root / "tests" / "scripts"))
        from golden_compare import compare
        a = {"data": {"nested": {"value": "actual"}}}
        b = {"data": {"nested": {"value": "golden"}}}
        diff = compare(a, b)
        assert "$.data.nested.value" in diff
        assert 'actual' in diff.lower() or '"actual"' in diff

    def test_cli_compare(self, repo_root, tmp_path):
        """golden_compare.py CLI exits 0 on a match (only timestamp differs)."""
        import json, subprocess
        a = {"prompts": {"x": 1}, "metadata": {"version": "1.0", "timestamp": "2026-01-01T00:00:00Z"}}
        b = {"prompts": {"x": 1}, "metadata": {"version": "1.0", "timestamp": "2026-12-31T00:00:00Z"}}
        af = tmp_path / "actual.json"
        bf = tmp_path / "golden.json"
        af.write_text(json.dumps(a))
        bf.write_text(json.dumps(b))
        result = subprocess.run(
            ["python3", str(repo_root / "tests/scripts/golden_compare.py"),
             "--no-validate", str(af), str(bf)],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 0

    def test_cli_mismatch(self, repo_root, tmp_path):
        """golden_compare.py CLI exits 1 on mismatch."""
        import json, subprocess
        a = {"prompts": {"x": 1}}
        b = {"prompts": {"x": 2}}
        af = tmp_path / "actual.json"
        bf = tmp_path / "golden.json"
        af.write_text(json.dumps(a))
        bf.write_text(json.dumps(b))
        result = subprocess.run(
            ["python3", str(repo_root / "tests/scripts/golden_compare.py"),
             "--no-validate", str(af), str(bf)],
            capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 1
        assert "GOLDEN MISMATCH" in result.stderr


class TestWriterRecording:
    """Run writer demo with recording to exercise recorder + channels + context."""

    def test_writer_recorded_trace(self, repo_root, engine):
        """Full writer run with recording produces complete trace with sub-contexts."""
        import autocog, json, os, tempfile
        from autocog.recorder import Recorder
        from autocog.context import Context
        from autocog.__main__ import load_externals

        prog = autocog.compile(
            str(repo_root / "share/demos/story-writer/writer.stl"),
            includes=[
                str(repo_root / "share/library"),
                str(repo_root / "share/demos/story-writer"),
            ],
        )
        externals = load_externals(prog, [
            str(repo_root / "share/library"),
            str(repo_root / "share/demos/story-writer"),
        ])

        with tempfile.TemporaryDirectory(prefix="autocog-test-") as tmpdir:
            recorder = Recorder(kinds="input,frame", path=tmpdir)
            ctx = Context(
                prog, engine, "init_idea",
                {"query": "bedtime story", "age": "3"},
                externals, recorder=recorder,
            )

            steps = 0
            while not ctx.done and steps < 50:
                ctx.step()
                steps += 1

            # Main context trace must exist
            trace_path = os.path.join(tmpdir, "ctx-0.json")
            assert os.path.isfile(trace_path), "main context trace missing"
            with open(trace_path) as f:
                trace = json.load(f)

            assert trace["ctx"] == 0
            assert trace["parent"] is None
            assert trace["entry"] == "init_idea"
            assert len(trace["trace"]) >= 4, f"expected ≥4 prompt steps, got {len(trace['trace'])}"

            # Check prompt sequence starts correctly
            prompts = [t["prompt"] for t in trace["trace"]]
            assert prompts[0] == "init_idea"
            assert "init_task" in prompts
            assert "init_draft" in prompts

            # At least one prompt should have calls (externals or sub-prompts)
            all_calls = [c for t in trace["trace"] for c in t.get("calls", [])]
            assert len(all_calls) > 0, "no channel calls recorded"

            # Sub-contexts should exist (create_pages, edit_title, reflexion)
            sub_ctx_files = [f for f in os.listdir(tmpdir) if f.startswith("ctx-") and f != "ctx-0.json"]
            assert len(sub_ctx_files) > 0, "no sub-context traces"

            # Check a sub-context has parent link
            for scf in sub_ctx_files:
                if scf.endswith(".json"):
                    with open(os.path.join(tmpdir, scf)) as f:
                        sub = json.load(f)
                    assert sub["parent"] == 0, f"sub-context {scf} parent != 0"
                    assert len(sub["trace"]) > 0
                    break

            # Step artifacts should exist in ctx-0/{prompt}/ directories
            ctx_dir = os.path.join(tmpdir, "ctx-0")
            assert os.path.isdir(ctx_dir), "ctx-0 artifact directory missing"
            artifact_files = []
            for root, dirs, files in os.walk(ctx_dir):
                for f in files:
                    if f.endswith(".json"):
                        artifact_files.append(os.path.join(root, f))
            assert len(artifact_files) >= 4, f"expected ≥4 step artifacts, got {len(artifact_files)}"

            # Each artifact is its own typed file (input/frame), named
            # {step}.{kind}.json with the artifact as its top-level content.
            for af in artifact_files:
                assert af.endswith(".input.json") or af.endswith(".frame.json"), \
                    f"unexpected artifact file {af}"
            with open(artifact_files[0]) as f:
                json.load(f)  # parses as a standalone document

    def test_writer_recorded_sub_context_pages(self, repo_root, engine):
        """Verify create_pages sub-contexts record their frames."""
        import autocog, json, os, tempfile
        from autocog.recorder import Recorder
        from autocog.context import Context
        from autocog.__main__ import load_externals

        prog = autocog.compile(
            str(repo_root / "share/demos/story-writer/writer.stl"),
            includes=[
                str(repo_root / "share/library"),
                str(repo_root / "share/demos/story-writer"),
            ],
        )
        externals = load_externals(prog, [
            str(repo_root / "share/library"),
            str(repo_root / "share/demos/story-writer"),
        ])

        with tempfile.TemporaryDirectory(prefix="autocog-test-") as tmpdir:
            recorder = Recorder(kinds="frame", path=tmpdir)
            ctx = Context(
                prog, engine, "init_idea",
                {"query": "bedtime story", "age": "3"},
                externals, recorder=recorder,
            )

            steps = 0
            while not ctx.done and steps < 50:
                ctx.step()
                steps += 1

            # Find create_pages sub-contexts
            create_pages_ctxs = []
            for f in sorted(os.listdir(tmpdir)):
                if f.startswith("ctx-") and f.endswith(".json") and f != "ctx-0.json":
                    with open(os.path.join(tmpdir, f)) as fh:
                        sub = json.load(fh)
                    if sub.get("entry") == "create_pages":
                        create_pages_ctxs.append((f, sub))

            assert len(create_pages_ctxs) >= 2, (
                f"expected ≥2 create_pages sub-contexts, got {len(create_pages_ctxs)}"
            )

            # Each should have a result (the returned pages)
            for fname, sub in create_pages_ctxs:
                assert "result" in sub, f"{fname} missing result"
                result = sub["result"]
                assert isinstance(result, list), f"{fname} result is not a list"
                assert len(result) > 0, f"{fname} returned empty pages"
