"""
Level 3 server — Backend.

Receives FTA JSON, evaluates against the model, returns raw text.
Thinnest server — just model inference.

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

    def evaluate_fta(fta: dict) -> str:
        """Evaluate an FTA and return the best path text."""
        import json
        fta_json = json.dumps(fta)
        ftt_id = backend_llama_cxx.evaluate_json(models[default_tag], fta_json)
        try:
            paths = backend_llama_cxx.get_best(models[default_tag], ftt_id, 1)
            return paths[0] if paths else ""
        finally:
            backend_llama_cxx.release_ftt(ftt_id)

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
