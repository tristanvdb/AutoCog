"""
Level 3 server — Backend.

Receives FTA JSON, evaluates against the model, returns the resulting FTT
plus the evaluation's perf deltas. Thinnest server — just model inference
(xfta over the wire). Walking the FTT into a frame is the client's
responsibility, using the program it holds locally.

    autocog backend --model model.gguf [--ctx N] [--port 8080] [--cpus 0-3]
    autocog backend --model '{"path": "a.gguf", "tag": "A", "ctx": 8192}' \
                    --model '{"path": "b.gguf", "tag": "B"}'

A backend is a bench *worker*: models are pre-assigned here (loaded once
at startup, each with its own load parameters), while syntax/search/
instantiation stay client-side (level 3). /capabilities advertises what
this worker hosts — tags plus per-model load parameters — so a campaign
launcher can route jobs by model.
"""

import os

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import Any, Dict, List, Optional

from . import RequestQueue, add_status_endpoint


class EvaluateRequest(BaseModel):
    fta: Dict[str, Any]
    model: Optional[str] = None    # model tag; default = the worker's default


class ScoreRequest(BaseModel):
    ftt: Dict[str, Any]            # encoder-produced (text-level) FTT
    model: Optional[str] = None


class SeedRequest(BaseModel):
    seed: int
    model: Optional[str] = None


class ResetRequest(BaseModel):
    kv: bool = True
    model: Optional[str] = None


class SubmitResponse(BaseModel):
    request_id: str


def default_tag(path):
    """Tag when the spec gives none: the filename with .gguf stripped."""
    name = os.path.basename(path)
    return name[:-5] if name.endswith(".gguf") else name


def create_app(models: List[Dict[str, Any]] = None, n_ctx: int = 4096) -> FastAPI:
    """Create the level-3 backend server.

    `models` is a list of load specs: {"path": ..., "tag": ..., "ctx": ...,
    "ngl": ..., "kv_slots": ...} — tag defaults to the filename sans .gguf,
    ctx to n_ctx, ngl/kv_slots to the AUTOCOG_* environment. The first spec
    is the worker's default model.

    The RNG model (tag="rng", model_id=0) is always available for testing;
    "rng" is a reserved tag that cannot be overridden. The FTA is
    self-contained (search params embedded by ista), so the backend needs
    no search config.
    """
    from autocog.backend.llama import backend_llama_cxx

    app = FastAPI(title="AutoCog Backend", description="Level 3: FTA evaluation")
    queue = RequestQueue()

    hosted = {"rng": {"id": 0, "ctx": None, "ngl": None, "kv_slots": None}}
    default = "rng"

    for spec in models or []:
        tag = spec.get("tag") or default_tag(spec["path"])
        if tag == "rng":
            raise ValueError('"rng" is a reserved model tag')
        if tag in hosted:
            raise ValueError(f"duplicate model tag {tag!r}")
        ctx = int(spec.get("ctx") or n_ctx)
        ngl = int(spec["ngl"]) if spec.get("ngl") is not None else -1
        kv = int(spec["kv_slots"]) if spec.get("kv_slots") is not None else -1
        model_id = backend_llama_cxx.create(spec["path"], ctx, ngl, kv)
        hosted[tag] = {"id": model_id, "ctx": ctx,
                       "ngl": ngl if ngl >= 0 else None,
                       "kv_slots": kv if kv >= 0 else None}
        if default == "rng":
            default = tag

    def resolve_model(tag):
        tag = tag or default
        if tag not in hosted:
            raise HTTPException(404, f"model {tag!r} not hosted here "
                                     f"(available: {list(hosted)})")
        return hosted[tag]["id"]

    def evaluate_fta(fta: dict, model: str = None) -> dict:
        """Evaluate an FTA; reply is {"ftt": ..., "perf": ...}.

        The backend is xfta over the wire: it evaluates the FTA against the
        model and returns the FTT plus the evaluation's autocog.perf.* deltas
        (the same field map xfta --perf emits — timing/counters ride the
        response, no side-channel). The client walks the FTT into a frame
        using the program it holds locally (single FTT->frame implementation
        in the runtime), so the backend does not walk it here.
        """
        import json as _json

        from autocog.runtime.sta import runtime_sta_cxx

        # The FTA arrived as a dict (FastAPI parsed the HTTP body). Hand it to
        # C++ to translate+store; C++ owns the structure from here.
        fta_id = runtime_sta_cxx.read_fta(fta)
        model_id = resolve_model(model)
        ftt_id, perf_json = backend_llama_cxx.evaluate(model_id, fta_id)
        try:
            return {"ftt": runtime_sta_cxx.get_ftt(ftt_id),
                    "perf": _json.loads(perf_json)}
        finally:
            runtime_sta_cxx.release_ftt(ftt_id)
            runtime_sta_cxx.release_fta(fta_id)

    def score_ftt(ftt: dict, model: str = None) -> dict:
        """Score an encoder-produced FTT (tokenize + forced P(token|prefix));
        reply is {"ftt": <scored>}. The client encodes frames locally (the
        runtime is model-free) and ships the text-level tree here."""
        from autocog.runtime.sta import runtime_sta_cxx

        ftt_id = runtime_sta_cxx.read_ftt(ftt)
        model_id = resolve_model(model)
        try:
            scored_id = backend_llama_cxx.score(model_id, ftt_id)
            try:
                return {"ftt": runtime_sta_cxx.get_ftt(scored_id)}
            finally:
                runtime_sta_cxx.release_ftt(scored_id)
        finally:
            runtime_sta_cxx.release_ftt(ftt_id)

    @app.on_event("startup")
    async def startup():
        await queue.start()

    @app.on_event("shutdown")
    async def shutdown():
        await queue.stop()

    @app.get("/models")
    async def list_models():
        """List available model tags."""
        return {"models": list(hosted.keys()), "default": default}

    @app.get("/capabilities")
    async def capabilities():
        """What this worker hosts and how it is pinned — the routing surface."""
        try:
            cpus = sorted(os.sched_getaffinity(0))
        except (AttributeError, OSError):  # non-Linux
            cpus = None
        return {
            "models": list(hosted.keys()),
            "details": {tag: {k: v for k, v in info.items() if k != "id"}
                        for tag, info in hosted.items()},
            "default": default,
            "n_ctx": n_ctx,
            "kv_slots": int(os.environ.get("AUTOCOG_KV_SLOTS", 0)) or None,
            "cpus": cpus,
            "pid": os.getpid(),
        }

    @app.post("/seed")
    async def seed(req: SeedRequest):
        backend_llama_cxx.set_seed(resolve_model(req.model), req.seed)
        return {"ok": True}

    @app.post("/reset")
    async def reset(req: ResetRequest):
        """Zero counters (and optionally drop KV) — measurement isolation."""
        backend_llama_cxx.reset(resolve_model(req.model), req.kv)
        return {"ok": True}

    @app.post("/evaluate")
    async def evaluate(req: EvaluateRequest) -> SubmitResponse:
        request_id = queue.submit(evaluate_fta, fta=req.fta, model=req.model)
        return SubmitResponse(request_id=request_id)

    @app.post("/score")
    async def score(req: ScoreRequest) -> SubmitResponse:
        request_id = queue.submit(score_ftt, ftt=req.ftt, model=req.model)
        return SubmitResponse(request_id=request_id)

    add_status_endpoint(app, queue)

    return app
