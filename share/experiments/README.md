# The benchmarking harness

**A separate application.** Nothing here is part of what AutoCog does: this
is the harness used to run campaigns against it, and it is a *client* of the
library like any other. It imports `autocog`; `autocog` never imports it.

```bash
cd ~/my-nfs && autocog/share/experiments/campaign.sh my-campaign.yml
```

```
share/experiments/
  campaign.sh       the entry point (bootstrap, then `python3 -m campaign`)
  campaign/         the app — see campaign/README.md
  perf/             compute cells; an idle machine, not a shared one
  dedup.py          which of a descriptor's syntaxes actually render apart
  tests/            its own suite
```

## Tests

Run on their own, and deliberately not in the repo's pytest job:

```bash
python3 -m pytest share/experiments/tests
```

The library's gates answer "is AutoCog broken?". A descriptor format, a
worker launcher or a results monitor failing is not an answer to that
question, and wiring them into the same job makes every harness change look
like a library regression — and every library run pay for a launcher that
spawns workers and loads a GGUF.

An installed `autocog` is a prerequisite, the same way it is for using the
harness at all.
