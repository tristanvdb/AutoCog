"""
Smoke tests — verify bindings load and basic pipeline works.
"""

import pytest


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
        while not ctx.done and steps < 50:
            ctx.step()
            steps += 1
        assert ctx.done, f"Writer did not complete after {steps} steps, at prompt={ctx.prompt}"
        assert steps <= 30, f"Writer took too many steps: {steps}"



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
                engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"))
                result = engine.run(prog, externals={}, topic="Sci", question="2+2?", choices=["3","4","5"])
                assert isinstance(result, str)
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
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
                engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"))
                externals = load_externals(prog, inc_paths)
                from autocog.context import Context
                ctx = Context(prog, engine, prog.entry_prompt("main"),
                              {"query": "bedtime", "age": "3"}, externals)
                steps = 0
                while not ctx.done and steps < 30:
                    ctx.step()
                    steps += 1
                assert ctx.done, f"Writer .stapp did not complete after {steps} steps"
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        finally:
            os.unlink(stapp_path)


class TestRemoteEngine:
    """Test RemoteEngine against in-process servers."""

    @staticmethod
    def _start_server(app, port):
        """Start a uvicorn server in a background thread."""
        import uvicorn
        import threading
        config = uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error")
        server = uvicorn.Server(config)
        thread = threading.Thread(target=server.run, daemon=True)
        thread.start()
        import time
        time.sleep(1)  # wait for startup
        return server

    def test_remote_run_level1(self, repo_root):
        """Test remote_run against a serve endpoint."""
        import autocog
        from autocog.server.serve import create_app
        from autocog.__main__ import load_externals

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax)
        server = self._start_server(app, 18091)
        try:
            result = autocog.remote_run(
                "http://127.0.0.1:18091",
                topic="Science", question="What is H2O?",
                choices=["Water", "Fire", "Air", "Earth"]
            )
            assert result in ["Water", "Fire", "Air", "Earth"]
        finally:
            server.should_exit = True

    def test_remote_engine_level2(self, repo_root):
        """Test RemoteEngine against an RPC endpoint."""
        import autocog
        from autocog.server.rpc import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax)
        server = self._start_server(app, 18092)
        try:
            engine = autocog.RemoteEngine("http://127.0.0.1:18092")
            result = engine.run(
                prog,
                topic="Science", question="What is H2O?",
                choices=["Water", "Fire", "Air", "Earth"]
            )
            assert result in ["Water", "Fire", "Air", "Earth"]
        finally:
            server.should_exit = True


class TestBackendServer:
    """Test the level-3 backend server."""

    @staticmethod
    def _start_server(app, port):
        import uvicorn, threading
        config = uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error")
        server = uvicorn.Server(config)
        thread = threading.Thread(target=server.run, daemon=True)
        thread.start()
        import time; time.sleep(1)
        return server

    def test_backend_evaluate(self, repo_root):
        """Submit FTA to backend, poll for result."""
        import autocog, json, urllib.request
        from autocog.runtime.sta import runtime_sta_cxx
        from autocog.server.backend import create_app

        # Compile and instantiate to get FTA JSON
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        engine = autocog.Engine(syntax=syntax)
        content = {"topic": "Science", "question": "2+2?", "choices": ["3", "4", "5"]}
        fta_id = runtime_sta_cxx.instantiate(
            prog.id, "main", content, engine.syntax_id
        )
        # Get FTA as JSON via the store
        from autocog.backend.llama import backend_llama_cxx
        # Use evaluate to verify the FTA works locally first
        ftt_id = backend_llama_cxx.evaluate(0, fta_id)
        backend_llama_cxx.release_ftt(ftt_id)

        # Export FTA JSON - instantiate again for the server test
        fta_id2 = runtime_sta_cxx.instantiate(
            prog.id, "main", content, engine.syntax_id
        )
        # Get the FTA JSON from the store via emit
        import autocog.compiler.stl.compiler_stl_cxx as cxx
        # Actually, we need the FTA JSON. Let's use ista approach:
        # instantiate returns an fta_id in the store, but we need JSON.
        # Use the xfta tool approach: serialize FTA from store
        runtime_sta_cxx.release_fta(fta_id)
        runtime_sta_cxx.release_fta(fta_id2)

        # Simpler: use stlc to compile, ista to get FTA JSON
        import subprocess, tempfile
        sta_json = json.dumps(prog.sta)
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            f.write(sta_json)
            sta_path = f.name
        try:
            ista_result = subprocess.run(
                ["ista", "-d", "/dev/stdin", sta_path],
                input=json.dumps(content),
                capture_output=True, text=True, timeout=10
            )
            assert ista_result.returncode == 0, f"ista failed: {ista_result.stderr}"
            fta_json = json.loads(ista_result.stdout)
        finally:
            import os; os.unlink(sta_path)

        # Start backend server
        app = create_app(model_path=None)  # RNG model
        server = self._start_server(app, 18093)
        try:
            # Test GET /models
            with urllib.request.urlopen("http://127.0.0.1:18093/models") as resp:
                models = json.loads(resp.read())
            assert "rng" in models["models"]

            # Test POST /evaluate
            req = urllib.request.Request(
                "http://127.0.0.1:18093/evaluate",
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
                with urllib.request.urlopen(f"http://127.0.0.1:18093/status/{request_id}") as resp:
                    status = json.loads(resp.read())
                if status["state"] == "complete":
                    assert isinstance(status["result"], str)
                    return
                elif status["state"] == "error":
                    raise AssertionError(f"Backend error: {status['error']}")
            raise AssertionError("Backend evaluation timed out")
        finally:
            server.should_exit = True


class TestServeServer:
    """Test the level-1 serve server including web form."""

    @staticmethod
    def _start_server(app, port):
        import uvicorn, threading
        config = uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error")
        server = uvicorn.Server(config)
        thread = threading.Thread(target=server.run, daemon=True)
        thread.start()
        import time; time.sleep(1)
        return server

    def test_web_form(self, repo_root):
        """Test GET / returns HTML form."""
        import autocog, urllib.request
        from autocog.server.serve import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax)
        server = self._start_server(app, 18094)
        try:
            with urllib.request.urlopen("http://127.0.0.1:18094/") as resp:
                html = resp.read().decode()
            assert "<form" in html
            assert "topic" in html
            assert "question" in html
            assert "choices" in html
        finally:
            server.should_exit = True

    def test_schema_endpoint(self, repo_root):
        """Test GET /schema returns entry point schemas."""
        import autocog, json, urllib.request
        from autocog.server.serve import create_app

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax)
        server = self._start_server(app, 18095)
        try:
            with urllib.request.urlopen("http://127.0.0.1:18095/schema") as resp:
                schema = json.loads(resp.read())
            assert "main" in schema
            assert "inputs" in schema["main"]
            assert "topic" in schema["main"]["inputs"]
        finally:
            server.should_exit = True


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
             "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}'],
            capture_output=True, text=True, timeout=30
        )
        assert result.returncode == 0
        assert result.stdout.strip() in ["3", "4", "5"]

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
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}'],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            assert result.stdout.strip() in ["3", "4", "5"]
        finally:
            os.unlink(sta_path)

    def test_run_input_file(self, repo_root):
        import subprocess, json, tempfile, os
        with tempfile.NamedTemporaryFile(mode='w', suffix=".json", delete=False) as f:
            json.dump({"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5"]}, f)
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
             "--rng", "-v", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}'],
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
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}',
                 "-o", outpath],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            with open(outpath) as f:
                assert f.read().strip() in ["3", "4", "5"]
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
                 "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}'],
                capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0
            assert result.stdout.strip() in ["3", "4", "5"]
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
                "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}']
        with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
            main()
        assert mock_out.getvalue().strip() in ["3", "4", "5"]

    def test_cmd_run_verbose(self, repo_root):
        import sys, io
        from unittest.mock import patch
        from autocog.__main__ import main

        args = ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                "--rng", "-v", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}']
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
                    "--rng", "--input", '{"topic":"Sci","question":"2+2?","choices":["3","4","5"]}']
            with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
                main()
            assert mock_out.getvalue().strip() in ["3", "4", "5"]
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
            json.dump({"topic": "Sci", "question": "2+2?", "choices": ["3", "4", "5"]}, f)
            input_path = f.name
        try:
            args = ["autocog", "run", "--stl", str(repo_root / "share/demos/mcq/select.stl"),
                    "--rng", "--input", input_path]
            with patch("sys.argv", args), patch("sys.stdout", new_callable=io.StringIO) as mock_out:
                main()
            assert mock_out.getvalue().strip() in ["3", "4", "5"]
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
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"))
        ctx = autocog.Context(prog, engine, "main",
                              {"topic": "X", "question": "Y", "choices": ["a", "b"]})
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
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"))
        externals = load_externals(prog, [str(repo_root / "share/demos/story-writer")])
        ctx = autocog.Context(prog, engine, prog.entry_prompt("main"),
                              {"query": "test", "age": "3"}, externals)
        steps = 0
        try:
            while not ctx.done and steps < 30:
                ctx.step()
                steps += 1
        except RuntimeError:
            pass  # Flow limit exceeded is expected with RNG
        assert steps > 2  # At least a few prompts executed

    def test_engine_run_async(self, repo_root):
        """Test async run path."""
        import asyncio, autocog
        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        engine = autocog.Engine(syntax=str(repo_root / "share/syntax/default.json"))
        result = asyncio.run(engine.run_async(
            prog, topic="Sci", question="2+2?", choices=["3", "4", "5"]
        ))
        assert result in ["3", "4", "5"]

    def test_remote_error_handling(self, repo_root):
        """Test RemoteEngine error path."""
        import autocog, json, urllib.request
        from autocog.server.rpc import create_app
        import uvicorn, threading, time

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax)
        config = uvicorn.Config(app, host="127.0.0.1", port=18096, log_level="error")
        server = uvicorn.Server(config)
        thread = threading.Thread(target=server.run, daemon=True)
        thread.start()
        time.sleep(1)

        try:
            # Request with bad prompt name should cause error
            engine = autocog.RemoteEngine("http://127.0.0.1:18096", poll_interval=0.2, timeout=5)
            try:
                engine.evaluate_prompt(prog, "nonexistent_prompt", {})
                assert False, "Should have raised"
            except RuntimeError as e:
                assert "failed" in str(e).lower() or "error" in str(e).lower()
        finally:
            server.should_exit = True

    def test_status_not_found(self, repo_root):
        """Test GET /status with unknown request_id returns 404."""
        import autocog, urllib.request, urllib.error, json
        from autocog.server.serve import create_app
        import uvicorn, threading, time

        prog = autocog.compile(str(repo_root / "share/demos/mcq/select.stl"))
        syntax = str(repo_root / "share/syntax/default.json")
        app = create_app(program=prog, model_path=None, syntax_path=syntax)
        config = uvicorn.Config(app, host="127.0.0.1", port=18097, log_level="error")
        server = uvicorn.Server(config)
        thread = threading.Thread(target=server.run, daemon=True)
        thread.start()
        time.sleep(1)

        try:
            try:
                urllib.request.urlopen("http://127.0.0.1:18097/status/nonexistent-id")
                assert False, "Should have raised"
            except urllib.error.HTTPError as e:
                assert e.code == 404
        finally:
            server.should_exit = True
