"""Every arm, once, on the pseudo-model, before anything is spent.

The validity gate (probe.check_answers) looks at ONE run's first syntax with
four real samples: it asks whether the answers coming back are usable. This
asks something cheaper and wider — does every (program, syntax) pair in the
descriptor execute at all?

It exists because a 332-arm descriptor generated from a syntax grid had 42
arms that raised IndexError out of the program's own resolver, and nothing
between writing the descriptor and paying for the box would have said so.
The gate that did exist would have passed: it only ever touched one arm.

`rng` needs no weights and no GPU, so this runs BEFORE provisioning — a
descriptor that cannot execute fails before a single byte of model is
downloaded, in seconds.

WHAT IT CANNOT SEE. An arm that runs and returns the WRONG answer. The
pseudo-model's answers are noise, so "plausible" is all any check here can
mean; a resolver that is off by one produces a perfectly plausible choice.
This bounds the damage of a broken arm to "silently wrong" instead of
"silently wrong or dead", which is worth the seconds, and is not a substitute
for reading the graphs.
"""

import os
import tempfile

from . import runner


def arms(descriptor, layout):
    """Distinct (program name, spec, syntax) triples across every run.

    Deduped: the same pair appears once per model arm, and rng answers say
    nothing about which model would have run it."""
    seen, out = set(), []
    for run in descriptor.runs:
        specs = runner.program_specs(descriptor, layout, run)
        for name, spec in specs.items():
            for syntax in run["syntaxes"]:
                if (name, syntax) in seen:
                    continue
                seen.add((name, syntax))
                out.append((name, spec, syntax))
    return out


def check(descriptor, layout, log=print, limit=0, syntax_paths=None):
    """Run one rng sample per arm. Returns [(program, syntax, error)]."""
    from autocog.bench.quality import run_quality

    todo = arms(descriptor, layout)
    if limit:
        todo = todo[:limit]
        log(f"preflight: LIMITED to {len(todo)} arm(s) — not a full check")
    log(f"preflight: executing {len(todo)} arm(s) on rng")

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, (name, spec, syntax) in enumerate(todo):
            events = run_quality(
                questions=1, syntaxes=[syntax], programs={name: spec},
                data=os.path.join(layout.repo, "share", "benchmarks",
                                  "quality", "questions.json"),
                out=os.path.join(tmp, str(i)), root=layout.repo,
                syntax_paths=syntax_paths, log=lambda *_: None)
            for ev in events:
                error = ev.get("error.message") or ev.get("autocog.bench.score_error")
                if error:
                    failures.append((name, syntax, error))
                elif ev.get("autocog.bench.answer") is None:
                    failures.append((name, syntax, "no answer"))
    return failures


def report(failures, log=print):
    """Group by (program, error kind) — 32 arms failing one way is one
    defect, and a list of 32 lines reads like 32."""
    grouped = {}
    for name, syntax, error in failures:
        grouped.setdefault((name, error.split(":")[0]), []).append((syntax, error))
    for (name, kind), items in sorted(grouped.items()):
        syntax, error = items[0]
        log(f"  {name}: {len(items)} arm(s) fail with {kind}")
        log(f"    e.g. {syntax}: {error[:160]}")
    return grouped
