"""
pytest configuration for autocog tests.

Tests run against the installed package (pip install -e . or pip install .).
"""

import pathlib

import pytest


@pytest.fixture(scope="session")
def repo_root():
    return pathlib.Path(__file__).parent.parent.parent.parent


@pytest.fixture(scope="session")
def share_dir(repo_root):
    return repo_root / "share"


@pytest.fixture(scope="session")
def syntax_path(share_dir):
    return str(share_dir / "syntax" / "default.json")


@pytest.fixture(scope="session")
def search_path(share_dir):
    return str(share_dir / "search" / "default.json")


LLAMA3_TEST_MODEL_URL = "https://huggingface.co/TensorBlock/tiny-llama3-test-GGUF/resolve/main/tiny-llama3-test-Q2_K.gguf"
LLAMA3_TEST_MODEL_MIN_SIZE = 1_000_000  # ~21MB expected


def _download_model(url, path, min_size):
    """Download a model file if missing. Returns path or None."""
    if path.exists() and path.stat().st_size > min_size:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        import urllib.request
        print(f"Downloading {path.name} from {url}...")
        urllib.request.urlretrieve(url, str(path))
        if path.stat().st_size > min_size:
            return path
        else:
            path.unlink(missing_ok=True)
    except Exception as e:
        print(f"Download failed: {e}")
        path.unlink(missing_ok=True)
    return None


@pytest.fixture
def engine(syntax_path, search_path):
    """Create a fresh RNG Engine per test.

    Function-scoped for isolation: each test gets its own engine so no engine
    state (or the handles it holds) leaks across tests. The real-model engine
    stays session-scoped to avoid reloading the GGUF every test.
    """
    import autocog
    return autocog.Engine(syntax=syntax_path, search=search_path)


@pytest.fixture(autouse=True)
def _collect_store_garbage():
    """Reclaim per-test Python handles at the test boundary.

    The C++ datastore is a process-global singleton and is content-addressed,
    so identical artifacts from different tests share one entry. A stale
    Program (whose __del__ releases its uid) must therefore be collected at the
    boundary between tests rather than mid-way through a later test, where it
    would release a uid that test still holds. Forcing a collection after each
    test keeps that lifecycle deterministic.
    """
    yield
    import gc
    gc.collect()


@pytest.fixture(autouse=True)
def reset_rng_seed(engine):
    """Reset RNG seed before each test for deterministic results."""
    engine.set_seed(42)


@pytest.fixture(scope="session")
def llama3_model_path(repo_root):
    """Path to TensorBlock/tiny-llama3-test Q2_K (21MB, 128k tokens).

    Full Llama 3 tokenizer — can run real STL programs.
    Downloads on first use if missing.

    The real model is required by default: if it can't be obtained, the
    dependent tests fail loudly rather than silently skipping (so CI needs no
    special configuration and never quietly drops the real-model coverage). Set
    AUTOCOG_NO_MINIMAL_GGUF to opt out — then an unavailable model skips those
    tests instead, for offline or air-gapped local development.
    """
    import os
    path = repo_root / "models" / "tiny-llama3-test-Q2_K.gguf"
    result = _download_model(LLAMA3_TEST_MODEL_URL, path, LLAMA3_TEST_MODEL_MIN_SIZE)
    if result is None:
        download_hint = (
            "tiny-llama3-test-Q2_K.gguf not available. Download manually:\n"
            f"  mkdir -p models && cd models && wget {LLAMA3_TEST_MODEL_URL}"
        )
        if os.environ.get("AUTOCOG_NO_MINIMAL_GGUF"):
            pytest.skip(download_hint)
        pytest.fail(
            "The real test model could not be obtained, so the real-model tests "
            "cannot run.\n" + download_hint +
            "\nTo skip these tests instead (e.g. offline development), set "
            "AUTOCOG_NO_MINIMAL_GGUF=1.",
            pytrace=False,
        )
    return str(result)


@pytest.fixture(scope="session")
def real_engine(syntax_path, search_path, llama3_model_path):
    """Engine with llama3-test model. Skips if model unavailable."""
    import autocog
    return autocog.Engine(model=llama3_model_path, syntax=syntax_path, search=search_path, n_ctx=2048)
