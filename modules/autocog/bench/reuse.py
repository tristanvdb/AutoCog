"""Sample identity, and keeping two campaigns from measuring the same thing.

Two separate jobs live here, at two different granularities, and the
distinction is the whole point:

**The sample fingerprint** identifies one measurement — this question,
under this syntax, by this program, on this model. It is what a run's
own resume uses to decide whether the rows already in its output
directory describe the experiment it is about to run. The per-run resume
keys it used to use, `(syntax, program, question)`, name a CELL of the
matrix, and the same cell measures something different the moment the
syntax file is edited. Edit a syntax, re-run, and the old numbers used
to survive.

**The question digest** identifies a datapoint, and nothing else. It is
what a CAMPAIGN uses to pick its questions: a campaign draws N of them
per dataset, once, and every experiment in it sees the same N. Given an
earlier campaign's questions, the next one draws from what is left.

Why the question and not the sample. Disjointness has to be decided at
the level a campaign chooses at, which is the dataset. If it were
decided per sample, a campaign that adds one syntax arm to last month's
matrix would be told to avoid last month's questions for the arms they
share — and then the new arm could not be compared to the old ones,
which is the only reason to add it. Choosing the question set once, at
the top, keeps the comparison inside a campaign exact and the sample
sets between campaigns disjoint.

Nothing is ever copied forward. Results are read by unioning campaign
directories, and a row sitting in two of them is counted twice by every
consumer that does.

WHAT THE FINGERPRINT DOES NOT COVER, stated because a digest invites
more trust than it earns:

  * The model TAG is a name, not a checksum of the weights. Pointing a
    worker's `Llama-3.2-1B` at different GGUF bytes produces a matching
    fingerprint and a different measurement. Tag discipline is the
    operator's, and it always was — the executor never sees a path.
  * The KV slot count. Measured: a given count is internally
    deterministic, but any two counts disagree on free-text logprobs
    (drift up to ~3e-3) while agreeing on tree shape. That makes it an
    experimental variable, and one the launcher MEASURES rather than
    declares. It is recorded on each event instead, so an analysis can
    group by it.
"""

import glob
import hashlib
import json
import os

#: bump when the digest's inputs change, so old rows stop matching rather
#: than matching for the wrong reason
FINGERPRINT_VERSION = 1


def canonical(obj):
    """A value's bytes for hashing: key order and spacing normalised."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False).encode("utf-8")


def digest(obj):
    return hashlib.sha256(canonical(obj)).hexdigest()


def json_digest(path):
    """A JSON config's digest, by CONTENT. Reformatting a syntax file is
    not a new experiment; changing a field is."""
    with open(path) as f:
        return digest(json.load(f))


def question_digest(question):
    """A datapoint's identity: its CONTENT, not its id.

    Two datasets can both call something `q17`, and one dataset rebuilt
    with a different converter can call the same id something else. Content
    also means a question cannot be excluded from one dataset by having
    appeared in an unrelated one — the digests simply never collide.
    """
    return digest({"v": FINGERPRINT_VERSION, "question": question})


def program_hash(program):
    """The compiled STA's own hash.

    Verified stable across the path the program was compiled from (so a
    campaign run from a different working directory still matches) and
    distinct between programs. It is taken from the artifact rather than
    the sources because the artifact is what actually ran: it already
    folds in the transitive imports and whatever the compiler did with
    the options.
    """
    return json.loads(program.dump_json())["metadata"]["hash"]


def fingerprint(model, syntax, search, program, question):
    """One measurement's identity. `syntax`/`search`/`program` are digests."""
    return digest({
        "v": FINGERPRINT_VERSION,
        "model": model,
        "syntax": syntax,
        "search": search,
        "program": program,
        "question": question,
    })


def prior_questions(dirs, log=None):
    """Every question digest an earlier campaign used, over its results.

    Two sources, because they fail in different ways. `samples-*.json` is
    the selection a campaign committed to up front — authoritative, and
    present even for arms that never ran. `results-*.ndjson` rows are the
    backstop for anything produced outside a campaign, or whose selection
    file was lost; error rows count here, unlike in a resume, because the
    question was still SPENT on and the point is not to draw it twice.

    Directories are scanned recursively: the digest says nothing about
    which campaign, phase or run directory it came from, which is what
    lets an earlier campaign with a different layout be excluded at all.
    """
    seen, files = set(), 0
    for root in dirs:
        for path in sorted(glob.glob(os.path.join(root, "**", "samples-*.json"),
                                     recursive=True)):
            files += 1
            with open(path) as f:
                for q in json.load(f):
                    seen.add(question_digest(q))
        for path in sorted(glob.glob(os.path.join(root, "**", "results-*.ndjson"),
                                     recursive=True)):
            files += 1
            with open(path, "rb") as f:
                for raw in f:
                    try:
                        ev = json.loads(raw)
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue        # torn last line of a killed run
                    fp = ev.get("autocog.bench.question.digest")
                    if fp:
                        seen.add(fp)
    if log:
        log(f"prior: {len(seen)} question(s) already used, from {files} "
            f"file(s) over {len(dirs)} "
            f"director{'y' if len(dirs) == 1 else 'ies'}")
    return seen
