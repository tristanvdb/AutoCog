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
