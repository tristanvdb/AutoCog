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


LLAMA3_TEST_MODEL_URL = "https://github.com/tristanvdb/AutoCog/releases/download/v0.5.0/tiny-llama3-test-Q2_K.gguf"
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


@pytest.fixture(scope="session")
def engine(syntax_path, search_path):
    """Create a shared Engine with RNG model for all tests."""
    import autocog
    return autocog.Engine(syntax=syntax_path, search=search_path)


@pytest.fixture(autouse=True)
def reset_rng_seed(engine):
    """Reset RNG seed before each test for deterministic results."""
    engine.set_seed(42)


@pytest.fixture(scope="session")
def llama3_model_path(repo_root):
    """Path to TensorBlock/tiny-llama3-test Q2_K (21MB, 128k tokens).

    Full Llama 3 tokenizer — can run real STL programs.
    Downloads on first use if missing. Skips test if unavailable.
    """
    path = repo_root / "models" / "tiny-llama3-test-Q2_K.gguf"
    result = _download_model(LLAMA3_TEST_MODEL_URL, path, LLAMA3_TEST_MODEL_MIN_SIZE)
    if result is None:
        pytest.skip(
            "tiny-llama3-test-Q2_K.gguf not available. Download manually:\n"
            f"  mkdir -p models && cd models && wget {LLAMA3_TEST_MODEL_URL}"
        )
    return str(result)


@pytest.fixture(scope="session")
def real_engine(syntax_path, search_path, llama3_model_path):
    """Engine with llama3-test model. Skips if model unavailable."""
    import autocog
    return autocog.Engine(model=llama3_model_path, syntax=syntax_path, search=search_path, n_ctx=2048)
