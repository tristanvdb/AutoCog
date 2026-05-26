# Story Writer Demo

A multi-prompt STL application that generates children's stories. Demonstrates loops, Python externals, call channels, dataflow, and the stdlib.

## Structure

- **writer.stl** — main program: idea generation, task planning, drafting, editing loop
- **book.stl** — page creation and title editing prompts
- **template.stl** — story template records (page, outline)
- **template.py** — Python external: lists available story templates
- **book.py** — Python external: writes the final formatted book
- **schema.json** — author-provided input/output schema descriptions

## Inputs

| Name | Type | Description |
|------|------|-------------|
| `query` | text | What kind of story to write (e.g. "bedtime story", "adventure") |
| `age` | enum 1–12 | Target reader age |

## Running

```bash
# Direct
autocog run --stl share/demos/story-writer/writer.stl \
    -I share/demos/story-writer --rng \
    --input '{"query":"bedtime story","age":"5"}'

# As .stapp
autocog pack --stl share/demos/story-writer/writer.stl \
    -I share/demos/story-writer -o writer.stapp

autocog run --app writer.stapp --rng \
    --input '{"query":"bedtime story","age":"5"}'

# Serve with web UI
autocog serve --app writer.stapp --model model.gguf --port 8080
```

## Flow

The program executes 12 steps through 11 prompts:

1. `init_idea` — generate story idea from query and age
2. `init_task` — create a task template (calls Python `list_templates`)
3. `init_topic` — refine the topic
4. `init_draft` — generate pages (calls `create_pages` per step, mapped)
5. `init_commit` — commit initial draft (calls Python `stlib_store`)
6. `loop_cond` — check if editing is complete (bounded loop, max 10 iterations)
7. `loop_collate` — collate feedback
8. `loop_analyse` — analyse what needs improvement
9. `loop_dispatch` — dispatch edits to pages
10. `loop_commit` — commit edits
11. `done` — terminal prompt

## Dependencies

Uses from stdlib: `thoughts.stl` (Thought record), `datastore.py` (key-value store).
