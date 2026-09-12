"""Worker abstraction: an engine factory over one loaded model.

A worker owns model weights (loaded once) and hands out Engines that
differ only in syntax/search — the shape a remote level-3 worker also
fits (models pre-assigned worker-side, syntax bound client-side).
"""

import os
import tempfile

from autocog.backend.llama import backend_llama_cxx
from autocog.engine import Engine


class LocalWorker:
    """In-process worker: one loaded model, engines cached per config.

    Args:
        model: GGUF path, or None for the RNG model
        n_ctx: context size (fixed at load)
        kv_slots: KV sequence-slot pool size (fixed at load; sets
            AUTOCOG_KV_SLOTS around model creation)
    """

    def __init__(self, model=None, n_ctx=4096, kv_slots=None):
        self.model_path = model
        self.n_ctx = n_ctx
        self.kv_slots = kv_slots
        if model is None:
            self.model_id = 0
        else:
            saved = os.environ.get("AUTOCOG_KV_SLOTS")
            try:
                if kv_slots is not None:
                    os.environ["AUTOCOG_KV_SLOTS"] = str(kv_slots)
                self.model_id = backend_llama_cxx.create(model, n_ctx)
            finally:
                if kv_slots is not None:
                    if saved is None:
                        os.environ.pop("AUTOCOG_KV_SLOTS", None)
                    else:
                        os.environ["AUTOCOG_KV_SLOTS"] = saved
        self._engines = {}
        self._tmp = None

    def capabilities(self):
        return {
            "models": [os.path.basename(self.model_path) if self.model_path else "rng"],
            "n_ctx": self.n_ctx,
            "kv_slots": self.kv_slots,
        }

    def engine(self, syntax, search):
        """Engine for (syntax, search) file paths, sharing this worker's model."""
        key = (syntax, search)
        if key not in self._engines:
            self._engines[key] = Engine(
                syntax=syntax, search=search, model_id=self.model_id)
        return self._engines[key]

    def engine_for_search_config(self, syntax, search_config):
        """Engine for a syntax path and an inline search-config dict (perf
        cells build their search per cell)."""
        import json

        if self._tmp is None:
            self._tmp = tempfile.mkdtemp(prefix="autocog-bench-")
        path = os.path.join(
            self._tmp, f"search-{abs(hash(json.dumps(search_config, sort_keys=True)))}.json")
        if not os.path.exists(path):
            with open(path, "w") as f:
                json.dump(search_config, f)
        return self.engine(syntax, path)

    def set_seed(self, seed):
        backend_llama_cxx.set_seed(self.model_id, seed)

    def reset(self, kv=True):
        """Zero counters (and drop KV slots) — isolation between measured runs."""
        backend_llama_cxx.reset(self.model_id, kv)
