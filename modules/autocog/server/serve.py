"""
Level 1 server — Full pipeline with web UI.

Serves a complete .stapp: channel resolution, evaluation, flow control.
Includes auto-generated web form from input schema.

    autocog serve --app writer.stapp --model model.gguf [--port 8080]
    autocog serve --stl program.stl -I includes/ --model model.gguf [--port 8080]
"""

from fastapi import FastAPI
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
from typing import Any, Dict, Optional

from . import RequestQueue, add_status_endpoint


class RunRequest(BaseModel):
    entry: str = "main"
    inputs: Dict[str, Any]


class SubmitResponse(BaseModel):
    request_id: str


def create_app(program=None, externals=None, model_path: str = None,
               syntax_path: str = None, n_ctx: int = 4096) -> FastAPI:
    """Create the level-1 serve app."""
    import autocog

    app = FastAPI(title="AutoCog Serve", description="Level 1: Full App Server")
    queue = RequestQueue()

    engine = autocog.Engine(
        model=model_path if model_path else None,
        syntax=syntax_path,
        n_ctx=n_ctx
    )
    _externals = externals or {}

    def run_program(entry: str, inputs: dict):
        """Run the full program to completion."""
        return engine.run(program, entry=entry, externals=_externals, **inputs)

    @app.on_event("startup")
    async def startup():
        await queue.start()

    @app.on_event("shutdown")
    async def shutdown():
        await queue.stop()

    @app.post("/run")
    async def submit_run(req: RunRequest) -> SubmitResponse:
        request_id = queue.submit(run_program, entry=req.entry, inputs=req.inputs)
        return SubmitResponse(request_id=request_id)

    @app.get("/schema")
    async def get_schema():
        """Return entry points with input/output schemas."""
        return program.entry_points

    @app.get("/", response_class=HTMLResponse)
    async def web_form():
        """Auto-generated web form from input schema."""
        return _generate_form_html(program)

    add_status_endpoint(app, queue)

    return app


def _generate_form_html(program) -> str:
    """Generate a simple HTML form from the program's input schema."""
    import json

    entries = program.entry_points
    entry_name = next(iter(entries))
    ep = entries[entry_name]
    inputs = ep.get("inputs", {})
    title = f"AutoCog — {entry_name}"

    fields_html = []
    for name, schema in inputs.items():
        label = schema.get("description", name)
        field_type = schema.get("type", "text")
        required = "required" if schema.get("required") else ""

        if "enum" in schema:
            options = "".join(f'<option value="{v}">{v}</option>' for v in schema["enum"])
            fields_html.append(
                f'<label for="{name}">{label}</label>'
                f'<select id="{name}" name="{name}" {required}>{options}</select>'
            )
        elif field_type == "array":
            placeholder = f"JSON array, e.g. [\"a\", \"b\", \"c\"]"
            fields_html.append(
                f'<label for="{name}">{label}</label>'
                f'<textarea id="{name}" name="{name}" placeholder="{placeholder}" '
                f'{required} rows="3"></textarea>'
            )
        else:
            fields_html.append(
                f'<label for="{name}">{label}</label>'
                f'<input type="text" id="{name}" name="{name}" {required}/>'
            )

    fields = "\n".join(f"<div class='field'>{f}</div>" for f in fields_html)
    schema_json = json.dumps(entries, indent=2)

    return f"""<!DOCTYPE html>
<html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  body {{ font-family: system-ui, sans-serif; max-width: 640px; margin: 40px auto; padding: 0 20px; }}
  .field {{ margin: 16px 0; }}
  label {{ display: block; font-weight: 600; margin-bottom: 4px; }}
  input, select, textarea {{ width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px; font-size: 14px; }}
  button {{ padding: 10px 24px; background: #2563eb; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; }}
  button:hover {{ background: #1d4ed8; }}
  #result {{ margin-top: 24px; padding: 16px; background: #f3f4f6; border-radius: 4px; white-space: pre-wrap; display: none; }}
  #status {{ margin-top: 8px; color: #6b7280; }}
  .spinner {{ display: none; }}
  .spinner.active {{ display: inline-block; animation: spin 1s linear infinite; }}
  @keyframes spin {{ to {{ transform: rotate(360deg); }} }}
</style>
</head><body>
<h1>{title}</h1>
<form id="form">
  {fields}
  <div class="field">
    <button type="submit">Run</button>
    <span class="spinner" id="spinner">⟳</span>
    <span id="status"></span>
  </div>
</form>
<pre id="result"></pre>
<script>
const ENTRY = {json.dumps(entry_name)};
const SCHEMA = {schema_json};

document.getElementById('form').onsubmit = async (e) => {{
  e.preventDefault();
  const form = e.target;
  const inputs = {{}};
  const inputSchema = SCHEMA[ENTRY].inputs || {{}};
  for (const [name, schema] of Object.entries(inputSchema)) {{
    let val = form.elements[name]?.value || '';
    if (schema.type === 'array') {{
      try {{ val = JSON.parse(val); }} catch(e) {{ val = val.split(',').map(s => s.trim()); }}
    }}
    inputs[name] = val;
  }}

  const status = document.getElementById('status');
  const spinner = document.getElementById('spinner');
  const result = document.getElementById('result');
  status.textContent = 'Submitting...';
  spinner.className = 'spinner active';
  result.style.display = 'none';

  const resp = await fetch('/run', {{
    method: 'POST', headers: {{'Content-Type': 'application/json'}},
    body: JSON.stringify({{ entry: ENTRY, inputs }})
  }});
  const {{ request_id }} = await resp.json();
  status.textContent = 'Running...';

  // Poll
  const poll = setInterval(async () => {{
    const s = await fetch('/status/' + request_id).then(r => r.json());
    if (s.state === 'complete') {{
      clearInterval(poll);
      spinner.className = 'spinner';
      status.textContent = 'Done';
      result.textContent = typeof s.result === 'string' ? s.result : JSON.stringify(s.result, null, 2);
      result.style.display = 'block';
    }} else if (s.state === 'error') {{
      clearInterval(poll);
      spinner.className = 'spinner';
      status.textContent = 'Error: ' + s.error;
    }}
  }}, 500);
}};
</script>
</body></html>"""
