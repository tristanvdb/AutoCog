"""
Level 2 server — Remote Prompt Call (RPC).

Receives {prompt, content}, instantiates, evaluates, parses,
returns the frame JSON. Client does channel resolution locally.

    autocog rpc --sta program.sta.json --model model.gguf [--port 8080]
    autocog rpc --app writer.stapp --model model.gguf [--port 8080]
"""

from fastapi import FastAPI
from pydantic import BaseModel
from typing import Any, Dict, Optional

from . import RequestQueue, add_status_endpoint


class PromptRequest(BaseModel):
    prompt: str
    content: Dict[str, Any]


class SubmitResponse(BaseModel):
    request_id: str


def create_app(program=None, model_path: str = None, syntax_path: str = None,
               search_path: str = None, n_ctx: int = 4096) -> FastAPI:
    """Create the level-2 RPC server."""
    import autocog

    app = FastAPI(title="AutoCog RPC", description="Level 2: Remote Prompt Call")
    queue = RequestQueue()

    engine = autocog.Engine(
        model=model_path if model_path else None,
        syntax=syntax_path,
        search=search_path,
        n_ctx=n_ctx
    )

    def evaluate_prompt(prompt: str, content: dict) -> dict:
        """Evaluate a single prompt and return the frame."""
        frame = engine.evaluate_prompt(program, prompt, content)
        return frame

    @app.on_event("startup")
    async def startup():
        await queue.start()

    @app.on_event("shutdown")
    async def shutdown():
        await queue.stop()

    @app.post("/prompt")
    async def submit_prompt(req: PromptRequest) -> SubmitResponse:
        request_id = queue.submit(evaluate_prompt, prompt=req.prompt, content=req.content)
        return SubmitResponse(request_id=request_id)

    @app.get("/schema")
    async def get_schema():
        """Return entry points with input/output schemas."""
        return program.entry_points

    add_status_endpoint(app, queue)

    return app
