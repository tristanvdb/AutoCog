"""Sample identity, and reusing a measurement a previous campaign paid for.

A quality sample costs GPU time, so running the same one twice is money
spent on an answer already in hand. This module decides when two samples
are THE SAME sample, and hands back the event that was already recorded.

The whole feature rests on the fingerprint, and the fingerprint is the
only part worth being paranoid about. `(syntax, program, question)` —
the triple the per-run resume uses — is not identity: it names a CELL of
the matrix, and the same cell measures something different the moment
the syntax file is edited, a program's source changes, or the search
config moves. Reusing across campaigns on the cell name alone would
silently mix two configurations into one dataset, which is exactly the
failure that makes a campaign's results unusable after the money is
spent. So the key is a digest of everything the descriptor determines:

    model tag       the arm being measured
    syntax          the JSON's CONTENT, canonicalised (reindenting a
                    syntax file is not a new experiment)
    search          same
    program         the STA's own hash — the compiled artifact, so it
                    covers the source, its imports, and the compile
                    options together. Hashing the .stl path or the
                    option list would miss an edited import.
    question        the datapoint's content, not its id: two datasets
                    can both call something `q17`.

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
    declares — putting it in the key would void every reuse whenever
    the box's worker layout changed. It is recorded on each event
    instead, so an analysis can group by it.

A mismatch always means RE-MEASURE. There is no "close enough" branch:
the point of the feature is to save money, and a fingerprint that
resolves doubt in favour of reuse saves money by producing a dataset
that cannot be trusted.
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
    """The sample's identity. `syntax`/`search`/`program` are digests."""
    return digest({
        "v": FINGERPRINT_VERSION,
        "model": model,
        "syntax": syntax,
        "search": search,
        "program": program,
        "question": question,
    })


class PriorIndex:
    """Fingerprint -> the event that measured it, over previous results.

    Built by scanning directories RECURSIVELY for `results-*.ndjson`. The
    fingerprint is complete, so which campaign or run directory a row came
    from does not matter — which is what lets a prior campaign be reused
    even though its phases, run labels and worker layout were different.

    Only offsets are held. A campaign's worth of quality events is ~65 KiB
    apiece (the full candidate distribution rides on every one), so an
    index that kept the events would be gigabytes; this one is ~100 bytes
    per sample and seeks for the few that are actually reused.

    Rows carrying `error.message` are skipped, for the same reason the
    per-run resume skips them: a sample whose worker died is not a
    measurement, and adopting it would make a transient failure permanent.
    """

    def __init__(self, dirs=(), log=None):
        self.dirs = list(dirs)
        self.offsets = {}
        self.files = 0
        self.rows = 0
        for d in self.dirs:
            self._scan(d)
        if log:
            log(f"prior: {len(self.offsets)} fingerprinted sample(s) from "
                f"{self.rows} row(s) in {self.files} file(s) over "
                f"{len(self.dirs)} director{'y' if len(self.dirs) == 1 else 'ies'}")

    def _scan(self, root):
        for path in sorted(glob.glob(os.path.join(root, "**", "results-*.ndjson"),
                                     recursive=True)):
            self.files += 1
            with open(path, "rb") as f:
                offset = 0
                for raw in f:
                    start, offset = offset, offset + len(raw)
                    try:
                        ev = json.loads(raw)
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue        # torn last line of a killed run
                    if ev.get("error.message"):
                        continue
                    fp = ev.get("autocog.bench.fingerprint")
                    if not fp:
                        continue        # pre-fingerprint vintage: not reusable
                    self.rows += 1
                    # Last writer wins: a re-measured sample supersedes.
                    self.offsets[fp] = (path, start)

    def __contains__(self, fp):
        return fp in self.offsets

    def __len__(self):
        return len(self.offsets)

    def get(self, fp):
        """The recorded event, stamped with where it was adopted from."""
        located = self.offsets.get(fp)
        if located is None:
            return None
        path, offset = located
        with open(path, "rb") as f:
            f.seek(offset)
            ev = json.loads(f.readline())
        # The measurement keeps its original @timestamp -- that is when it
        # was measured, and rewriting it would erase the only evidence that
        # this row was not produced by the run it now sits in.
        ev["autocog.bench.reused.from"] = path
        ev["autocog.bench.reused.at"] = ev.get("@timestamp")
        return ev
