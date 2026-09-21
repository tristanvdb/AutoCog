# Performance benchmarking

Separate from campaigns on purpose. The two measurements want opposite things
from a machine:

| | quality campaign | performance run |
|---|---|---|
| machine | whatever is cheapest today | controlled, known |
| load | every lane saturated | exclusive, nothing else running |
| output | an accuracy | a number you can publish |

A campaign schedules across workers to keep them all busy, which is exactly
what makes its timings meaningless. So perf has no descriptor, no planner and
no worker pool — each script here is a thin call to `autocog bench perf` with
one cells file and one model, on a box you are not sharing.

```bash
autocog/share/experiments/perf/cells.sh bottleneck models/Llama-3.2-1B-Instruct-Q8_0.gguf
autocog/share/experiments/perf/cells.sh termination --rng
```

Cell files live in `share/benchmarks/compute/` alongside the programs they
run. A cell is one point in the search-parameter space; see that directory
for what the fields mean.

The campaign app measures capacity too — how many workers fit, roughly how
fast — but that output is a scheduling decision and never a published number.
Don't mix the two.
