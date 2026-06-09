"""
Level 3 server — Backend.

Receives FTA JSON, evaluates against the model, returns the resulting FTT.
Thinnest server — just model inference (xfta over the wire). Walking the FTT
into a frame is the client's responsibility, using the program it holds locally.

    autocog backend --model model.gguf [--ctx N] [--port 8080]
"""

import os

from fastapi import FastAPI
from pydantic import BaseModel
from typing import Any, Dict

from . import RequestQueue, add_status_endpoint


class EvaluateRequest(BaseModel):
    fta: Dict[str, Any]


class SubmitResponse(BaseModel):
    request_id: str


def create_app(model_path: str = None, n_ctx: int = 4096) -> FastAPI:
    """Create the level-3 backend server.

    The RNG model (tag="rng") is always available for testing,
    even when no real model is loaded. When multi-model support
    is added, "rng" is a reserved tag that cannot be overridden.

    The FTA is self-contained (search params embedded by ista),
    so the backend needs no search config.
    """
    from autocog.backend.llama import backend_llama_cxx

    app = FastAPI(title="AutoCog Backend", description="Level 3: FTA evaluation")
    queue = RequestQueue()

    # RNG model is always available (model_id=0, tag="rng")
    models = {"rng": 0}
    default_tag = "rng"

    if model_path:
        model_id = backend_llama_cxx.create(model_path, n_ctx)
        tag = os.path.splitext(os.path.basename(model_path))[0]
        models[tag] = model_id
        default_tag = tag

    def evaluate_fta(fta: dict) -> dict:
        """Evaluate an FTA and return the resulting FTT.

        The backend is xfta over the wire: it evaluates the FTA against the
        model and returns the FTT. The client walks the FTT into a frame using
        the program it holds locally (single FTT->frame implementation in the
        runtime), so the backend does not walk it here.
        """
        from autocog.runtime.sta import runtime_sta_cxx

        # The FTA arrived as a dict (FastAPI parsed the HTTP body). Hand it to
        # C++ to translate+store; C++ owns the structure from here.
        fta_id = runtime_sta_cxx.read_fta(fta)
        model_id = models[default_tag]
        ftt_id = backend_llama_cxx.evaluate(model_id, fta_id)
        try:
            return runtime_sta_cxx.get_ftt(ftt_id)
        finally:
            runtime_sta_cxx.release_ftt(ftt_id)
            runtime_sta_cxx.release_fta(fta_id)

    @app.on_event("startup")
    async def startup():
        await queue.start()

    @app.on_event("shutdown")
    async def shutdown():
        await queue.stop()

    @app.get("/models")
    async def list_models():
        """List available model tags."""
        return {"models": list(models.keys()), "default": default_tag}

    @app.post("/evaluate")
    async def evaluate(req: EvaluateRequest) -> SubmitResponse:
        request_id = queue.submit(evaluate_fta, fta=req.fta)
        return SubmitResponse(request_id=request_id)

    add_status_endpoint(app, queue)

    return app
