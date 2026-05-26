"""
pytest configuration — sets up paths to find C++ bindings.

Run with: BUILD_DIR=/path/to/build pytest tests/python/
Or from the build directory: cd build && pytest ../tests/python/
"""

import os
import sys
import pathlib

import pytest


def find_build_dir():
    """Find the build directory containing the compiled bindings."""
    # 1. Explicit BUILD_DIR env var
    env = os.environ.get("BUILD_DIR")
    if env and pathlib.Path(env).is_dir():
        return pathlib.Path(env)

    # 2. Common locations relative to repo root
    repo = pathlib.Path(__file__).parent.parent.parent.parent
    for candidate in [
        repo / "builds" / "autocog-dbg",
        repo / "builds" / "autocog",
        repo / "build",
    ]:
        if candidate.is_dir():
            return candidate

    return None


@pytest.fixture(scope="session", autouse=True)
def setup_paths():
    """Add binding directories to sys.path before any imports."""
    build = find_build_dir()
    if build is None:
        pytest.skip("No build directory found. Set BUILD_DIR env var.")

    binding_dirs = [
        build / "bindings" / "compiler-stl",
        build / "bindings" / "runtime-sta",
        build / "bindings" / "backend-llama",
    ]
    for d in binding_dirs:
        if d.is_dir():
            sys.path.insert(0, str(d))

    # Also add the modules directory
    repo = pathlib.Path(__file__).parent.parent.parent.parent
    modules = repo / "modules"
    if modules.is_dir():
        sys.path.insert(0, str(modules))


@pytest.fixture(scope="session")
def repo_root():
    return pathlib.Path(__file__).parent.parent.parent.parent


@pytest.fixture(scope="session")
def share_dir(repo_root):
    return repo_root / "share"


@pytest.fixture(scope="session")
def syntax_path(share_dir):
    return str(share_dir / "syntax" / "default.json")


LLAMA3_TEST_MODEL_URL = "https://github.com/tristanvdb/AutoCog/releases/download/test-fixtures-v1/tiny-llama3-test-Q2_K.gguf"
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
def engine(syntax_path):
    """Create a shared Engine with RNG model for all tests."""
    import autocog
    return autocog.Engine(syntax=syntax_path)


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
def real_engine(syntax_path, llama3_model_path):
    """Engine with llama3-test model. Skips if model unavailable."""
    import autocog
    return autocog.Engine(model=llama3_model_path, syntax=syntax_path, n_ctx=2048)
