# REST API

All three server levels share an async request pattern: submit a request, receive a request ID, poll for the result.

## Common: Status Polling

Available on all server levels.

### GET /status/{request_id}

Poll the status of a submitted request.

**Response:**

```json
{
  "request_id": "uuid-string",
  "state": "pending|running|complete|error",
  "result": null,
  "error": null
}
```

| State | Description |
|-------|-------------|
| `pending` | Queued, not yet started |
| `running` | Currently being evaluated |
| `complete` | Finished — `result` contains the output |
| `error` | Failed — `error` contains the message |

## Backend (Level 3)

Started with `autocog backend`. Evaluates FTA JSON against a model.

### POST /evaluate

Submit an FTA for evaluation.

**Request:**

```json
{
  "fta": { ... }
}
```

The `fta` field contains a complete FTA JSON object (as produced by `ista` or `runtime_sta_cxx.instantiate`).

**Response:**

```json
{
  "request_id": "uuid-string"
}
```

Poll `GET /status/{request_id}` until complete. The `result` field contains the raw text output (best path).

### GET /models

List available model tags.

**Response:**

```json
{
  "models": ["rng", "SmolLM3-Q4_K_M"],
  "default": "SmolLM3-Q4_K_M"
}
```

The `rng` tag is always available for testing.

## RPC (Level 2)

Started with `autocog rpc`. Evaluates a single prompt given resolved content.

### POST /prompt

Submit a prompt for evaluation.

**Request:**

```json
{
  "prompt": "init_idea",
  "content": {
    "query": "bedtime story",
    "age": "5"
  }
}
```

The `prompt` field is the mangled prompt name. The `content` field contains the fully-resolved channel values (the client does channel resolution).

**Response:**

```json
{
  "request_id": "uuid-string"
}
```

Poll `GET /status/{request_id}` until complete. The `result` field contains the parsed frame (dict of field values).

### GET /schema

Return entry points with input/output schemas.

**Response:**

```json
{
  "main": {
    "prompt": "init_idea",
    "inputs": {
      "query": {"type": "text", "required": true},
      "age": {"type": "text", "enum": ["1","2",...], "required": true}
    },
    "outputs": {
      "done": {"type": "text", "required": true}
    }
  }
}
```

## Serve (Level 1)

Started with `autocog serve`. Runs a complete app — channel resolution, multi-prompt flow, and evaluation.

### POST /run

Submit a program execution.

**Request:**

```json
{
  "entry": "main",
  "inputs": {
    "query": "bedtime story",
    "age": "5"
  }
}
```

The `entry` field selects the entry point (default: `"main"`). The `inputs` field contains the external input values.

**Response:**

```json
{
  "request_id": "uuid-string"
}
```

Poll `GET /status/{request_id}` until complete. The `result` field contains the program's return value.

### GET /schema

Same as RPC level — returns entry points with schemas.

### GET /

Auto-generated HTML form. Renders input fields from the schema (text inputs, dropdowns for enums, textareas for arrays). Submits via the `/run` endpoint and polls for results.

## Client Example

### Python (urllib)

```python
import autocog

# Level 1 — full remote execution
result = autocog.remote_run("http://server:8080", query="test", age="5")

# Level 2 — local channels, remote evaluation
engine = autocog.RemoteEngine("http://server:8080")
result = engine.run(program, externals=externals, query="test", age="5")
```

### curl

```bash
# Submit
REQ_ID=$(curl -s -X POST http://localhost:8080/run \
    -H "Content-Type: application/json" \
    -d '{"entry":"main","inputs":{"query":"test","age":"5"}}' \
    | jq -r .request_id)

# Poll
curl -s http://localhost:8080/status/$REQ_ID | jq .
```
