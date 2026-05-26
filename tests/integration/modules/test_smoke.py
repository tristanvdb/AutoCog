"""
Smoke tests — verify bindings load and basic pipeline works.
"""

import pytest


class TestBindings:
    """Verify all three bindings import and respond."""

    def test_compiler_import(self):
        import compiler_stl_cxx
        assert hasattr(compiler_stl_cxx, "compile")

    def test_runtime_import(self):
        import runtime_sta_cxx
        assert hasattr(runtime_sta_cxx, "instantiate")

    def test_backend_import(self):
        import backend_llama_cxx
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
        assert result in ["1", "2", "3", "4"]

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
        import backend_llama_cxx
        assert backend_llama_cxx.vocab_size(real_engine.model_id) == 128256

    def test_llama3_tokenize_roundtrip(self, real_engine):
        """Verify tokenization roundtrip with real tokenizer."""
        import backend_llama_cxx
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
        assert result in ["1", "2", "3", "4"]

    def test_llama3_call_basic(self, real_engine, repo_root):
        """Run call_basic with the real model — sub-prompt execution."""
        import autocog
        prog = autocog.compile(
            str(repo_root / "tests/fixtures/stl/flows/calls/test_call_basic.stl")
        )
        result = real_engine.run(prog, topic="science")
        assert isinstance(result, str)
        assert len(result) > 0
