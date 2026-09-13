#!/usr/bin/env bash
# Campaign launcher — run campaign descriptor(s) against this machine.
#
#     cd ~/my-nfs
#     autocog/share/experiments/campaign.sh campaigns/v08.json [more.json ...]
#
# A campaign descriptor declares experiments and requirements (models
# with load params, datasets, a worker MODE); it never knows the
# hardware. This launcher adapts each campaign to the machine:
#
#   "worker": {"mode": "isolated"}            one worker, whole machine,
#                                             hosting ALL campaign models
#   "worker": {"mode": "distributed",         one worker per GPU x factor,
#              "factor": 3}                   one model each (fail-fast if
#                                             models > slots)
#
# Each campaign is a transaction: verify (models+datasets present) ->
# spawn workers -> run phases in order (probe phases via the probe tool,
# the rest via `autocog bench campaign --phase ... --worker ...`) ->
# archive results -> kill workers -> next campaign.
#
# Options:
#   --dryrun     validate + print the full plan (workers, routing, outputs),
#                spawn and run nothing
#   --ngl N      GPU layers for workers (default: AUTOCOG_NGL, else 99)
#   --phase P    restrict to one phase (repeatable; for reruns/debugging)
#
# Foreground by design: progress on stdout; if the terminal dies, rerun —
# the executor and the probe tool both skip runs whose outputs exist.
# GPU count comes from the calibration profile (.calibration/) when
# present, else nvidia-smi; a GPU-less machine gets one worker slot.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

NGL="${AUTOCOG_NGL:-99}"
DRYRUN=0
PHASES=()
DESCRIPTORS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --dryrun) DRYRUN=1; shift ;;
        --ngl)    NGL="$2"; shift 2 ;;
        --phase)  PHASES+=("$2"); shift 2 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) DESCRIPTORS+=("$1"); shift ;;
    esac
done
[ "${#DESCRIPTORS[@]}" -ge 1 ] || { echo "usage: campaign.sh [--dryrun] <descriptor.json> ..." >&2; exit 2; }

# shellcheck disable=SC1091
source "$VENV/bin/activate"
export AUTOCOG_WORKDIR="$WORKDIR" AUTOCOG_REPO="$REPO"
export DATASETS_PATH="${DATASETS_PATH:-$DATASETS_DIR}"
export RESULTS_PATH="${RESULTS_PATH:-$RESULTS_DIR}"
EVENTS="$RESULTS_PATH/campaign-events.ndjson"

emit_event() {  # emit_event '<json>' — launcher lifecycle into the stream
    python3 -c "
import json, sys
from datetime import datetime, timezone
ev = json.loads(sys.argv[1])
ev['@timestamp'] = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.') \
    + f'{datetime.now(timezone.utc).microsecond // 1000:03d}Z'
print(json.dumps(ev))" "$1" >> "$EVENTS"
}

# GPU inventory: calibration profile first, then nvidia-smi, then none.
gpu_count() {
    local prof
    prof=$(ls "$WORKDIR/.calibration"/profile-*.json 2>/dev/null | head -1)
    if [ -n "$prof" ]; then
        python3 -c "import json; print(json.load(open('$prof'))['gpus'])" 2>/dev/null && return
    fi
    if command -v nvidia-smi > /dev/null 2>&1; then
        nvidia-smi -L 2>/dev/null | grep -c . || echo 0
    else
        echo 0
    fi
}
GPUS=$(gpu_count)

# plan_campaign <descriptor> <gpus> — emits a shell-sourceable plan:
#   PLAN_ERR (fatal message) or CAMPAIGN name, WORKER_SPECS (one JSON
#   array of model specs per worker, per line), PHASE_LIST, PROBE_<phase>
plan_json() {
    python3 - "$1" "$2" "$MODELS_DIR" "$DATASETS_PATH" <<'EOF'
import json, os, sys
desc_path, gpus, models_dir, datasets_dir = sys.argv[1:5]
gpus = int(gpus)
desc = json.load(open(desc_path))
out = {"name": desc.get("name") or os.path.splitext(os.path.basename(desc_path))[0]}

def fail(msg):
    print(json.dumps({"error": msg})); sys.exit(0)

# -- verify models --------------------------------------------------------
def find_model(tag):
    if tag == "rng":
        return None
    exact = os.path.join(models_dir, tag + ".gguf")
    if os.path.isfile(exact):
        return exact
    import glob as g, re
    hits = sorted(g.glob(os.path.join(models_dir, tag + "*.gguf")))
    if len(hits) > 1:
        # A base tag prefix-matches its -Instruct sibling; keep only hits
        # whose remainder is a quant suffix (".Q8_0", "-Q4_K_M", ...).
        quant = [h for h in hits if re.match(
            r"^[.-]?[QqFf][0-9]", os.path.basename(h)[len(tag):])]
        if len(quant) == 1:
            return quant[0]
    if len(hits) == 1:
        return hits[0]
    fail(f"model {tag!r}: " + ("not found" if not hits else f"ambiguous {hits}")
         + f" under {models_dir}")

models = []
for m in desc.get("models", []):
    spec = dict(m) if isinstance(m, dict) else {"model": m}
    tag = spec.pop("model")
    path = find_model(tag)
    entry = {"tag": tag, "path": path}
    entry.update(spec)          # ctx, ngl, kv_slots ride along
    models.append(entry)

# -- verify datasets ------------------------------------------------------
missing = []
for d in desc.get("datasets", []):
    cands = [d, os.path.join(datasets_dir, d), os.path.join(datasets_dir, d + ".json"),
             os.path.join(datasets_dir, d + ".jsonl")]
    if not any(os.path.isfile(c) for c in cands):
        missing.append(d)
if missing:
    fail(f"datasets not found under {datasets_dir}: {missing}")

# -- worker topology ------------------------------------------------------
wconf = desc.get("worker", {"mode": "isolated"})
mode = wconf.get("mode", "isolated")
slots = []   # each: {"gpu": int|None, "models": [entry...]}
real = [m for m in models if m["path"]]
if mode == "isolated":
    slots = [{"gpu": None, "models": real}]          # whole machine, all models
elif mode == "distributed":
    factor = int(wconf.get("factor", 1))
    n = max(gpus, 1) * factor
    if len(real) > n:
        fail(f"{len(real)} models but only {n} worker slot(s) "
             f"({max(gpus,1)} GPU(s) x factor {factor}) — raise factor")
    slots = [{"gpu": (i % gpus) if gpus else None, "models": [real[i % len(real)]] if real else []}
             for i in range(min(n, max(len(real), 1)))]
else:
    fail(f"unknown worker mode {mode!r}")

# -- phases ---------------------------------------------------------------
phases = []
for p in desc.get("phases", []):
    kinds = {r.get("kind") for r in p.get("runs", [])}
    if "probe" in kinds and kinds != {"probe"}:
        fail(f"phase {p.get('name')!r} mixes probe and executor runs")
    phases.append({"name": p.get("name"), "probe": kinds == {"probe"},
                   "runs": p.get("runs", [])})

out.update({"models": models, "slots": slots, "phases": phases, "mode": mode})
print(json.dumps(out))
EOF
}

wait_ready() {  # url pid log timeout -- fail FAST when the worker died
    local t=0
    while [ "$t" -lt "$4" ]; do
        curl -fsS "http://$1/capabilities" > /dev/null 2>&1 && return 0
        if ! kill -0 "$2" 2>/dev/null; then
            echo "FATAL: worker $1 exited during startup; log tail:" >&2
            tail -5 "$3" >&2
            return 1
        fi
        sleep 1; t=$((t + 1))
    done
    echo "FATAL: worker $1 not ready after $4s (log: $3)" >&2
    return 1
}

for DESC in "${DESCRIPTORS[@]}"; do
    PLAN=$(plan_json "$DESC" "$GPUS")
    ERR=$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('error',''))" "$PLAN")
    if [ -n "$ERR" ]; then echo "FATAL [$DESC]: $ERR" >&2; exit 1; fi
    NAME=$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['name'])" "$PLAN")
    N_SLOTS=$(python3 -c "import json,sys; print(len(json.loads(sys.argv[1])['slots']))" "$PLAN")
    MODE=$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['mode'])" "$PLAN")

    echo "=== campaign $NAME [$MODE, $N_SLOTS worker(s), $GPUS GPU(s)] ==="
    if [ "$DRYRUN" = 0 ]; then
        mkdir -p "$RESULTS_PATH"
        PLAN_EV=$(python3 -c "
import json, sys
plan = json.loads(sys.argv[1])
desc = json.load(open(sys.argv[2]))
print(json.dumps({'event.action': 'campaign.plan', 'campaign': plan['name'],
                  'phases': [{'name': p['name'], 'runs': len(p['runs'])}
                             for p in plan['phases']],
                  'axes': desc.get('axes', [])}))" "$PLAN" "$DESC")
        emit_event "$PLAN_EV"
        emit_event "{\"event.action\": \"campaign.start\", \"campaign\": \"$NAME\"}"
        echo "status screen:  python3 $REPO/share/benchmarks/monitor.py $EVENTS"
    fi
    if [ "$DRYRUN" = 1 ]; then
        python3 - "$PLAN" <<'EOF'
import json, sys
plan = json.loads(sys.argv[1])
for i, s in enumerate(plan["slots"]):
    tags = ", ".join(m["tag"] for m in s["models"]) or "rng"
    print(f"  worker-{i}: gpu={s['gpu'] if s['gpu'] is not None else '-'} models=[{tags}]")
for p in plan["phases"]:
    kind = "probe" if p["probe"] else "bench"
    runs = ", ".join(f"{r.get('kind')}:{r.get('model','rng')}" for r in p["runs"])
    print(f"  phase {p['name']} [{kind}]: {runs}")
EOF
        echo "  results -> $RESULTS_PATH/$NAME  (dryrun: nothing spawned)"
        continue
    fi

    # -- spawn workers ----------------------------------------------------
    PIDS=() ; URLS=()
    WLOG="$RESULTS_PATH/$NAME/workers"
    mkdir -p "$WLOG"
    for i in $(seq 0 $((N_SLOTS - 1))); do
        PORT=$((17700 + i))
        SPECS=$(python3 - "$PLAN" "$i" "$NGL" <<'EOF'
import json, sys
plan, i, ngl = json.loads(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
args = []
for m in plan["slots"][i]["models"]:
    spec = {"path": m["path"], "tag": m["tag"]}
    for k in ("ctx", "ngl", "kv_slots"):
        if m.get(k) is not None:
            spec[k] = m[k]
    spec.setdefault("ngl", ngl)
    args += ["--model", json.dumps(spec)]
print("\n".join(args) if args else "--rng")
EOF
)
        GPU=$(python3 -c "import json,sys; g=json.loads(sys.argv[1])['slots'][$i]['gpu']; print('' if g is None else g)" "$PLAN")
        CMD=(python3 -m autocog backend --host 127.0.0.1 --port "$PORT")
        while IFS= read -r a; do [ -n "$a" ] && CMD+=("$a"); done <<< "$SPECS"
        if [ -n "$GPU" ]; then
            CUDA_VISIBLE_DEVICES="$GPU" "${CMD[@]}" > "$WLOG/worker-$i.log" 2>&1 &
        else
            "${CMD[@]}" > "$WLOG/worker-$i.log" 2>&1 &
        fi
        PIDS+=($!) ; URLS+=("127.0.0.1:$PORT")
    done
    trap 'kill "${PIDS[@]}" 2>/dev/null || true' EXIT
    for i in "${!URLS[@]}"; do
        wait_ready "${URLS[$i]}" "${PIDS[$i]}" "$WLOG/worker-$i.log" 600 || exit 1
        echo "[up] ${URLS[$i]}"
    done

    # -- run phases in order ----------------------------------------------
    WARGS=() ; for u in "${URLS[@]}"; do WARGS+=(--worker "$u"); done
    PHASE_NAMES=$(python3 -c "import json,sys; print('\n'.join(p['name'] for p in json.loads(sys.argv[1])['phases']))" "$PLAN")
    while IFS= read -r PH; do
        if [ "${#PHASES[@]}" -gt 0 ]; then
            match=0; for want in "${PHASES[@]}"; do [ "$want" = "$PH" ] && match=1; done
            [ "$match" = 1 ] || continue
        fi
        IS_PROBE=$(python3 -c "import json,sys; p=[p for p in json.loads(sys.argv[1])['phases'] if p['name']==sys.argv[2]][0]; print(1 if p['probe'] else 0)" "$PLAN" "$PH")
        if [ "$IS_PROBE" = 1 ]; then
            python3 - "$PLAN" "$PH" "$RESULTS_PATH/$NAME" "$EVENTS" <<'EOF' || echo "!!! probe phase $PH reported failures"
import json, os, subprocess, sys
from datetime import datetime, timezone
plan, phase_name, out_root, events = (json.loads(sys.argv[1]), sys.argv[2],
                                      sys.argv[3], sys.argv[4])
repo = os.environ["AUTOCOG_REPO"]
tool = os.path.join(repo, "share", "benchmarks", "probe_choices.py")
phase = [p for p in plan["phases"] if p["name"] == phase_name][0]
urls = [f"127.0.0.1:{17700 + i}" for i in range(len(plan["slots"]))]
hosted = [{m["tag"] for m in s["models"]} or {"rng"} for s in plan["slots"]]

def lifecycle(action, label, run, **extra):
    ts = datetime.now(timezone.utc)
    ev = {"@timestamp": ts.strftime("%Y-%m-%dT%H:%M:%S.")
          + f"{ts.microsecond // 1000:03d}Z",
          "event.action": action,
          "autocog.campaign.name": plan["name"],
          "autocog.campaign.phase": phase_name,
          "autocog.campaign.run": label, "autocog.campaign.kind": "probe",
          "autocog.campaign.model": run.get("model", "rng")}
    ev.update(extra)
    with open(events, "a") as f:
        f.write(json.dumps({"autocog.bench": ev}) + "\n")

rc = 0
for i, run in enumerate(phase["runs"]):
    tag = run.get("model", "rng")
    label = run.get("out") or f"{phase_name}-{i}"
    url = next((u for u, h in zip(urls, hosted) if tag in h), None)
    if url is None:
        print(f"!!! probe {tag}: no worker hosts it")
        lifecycle("run.end", label, run, **{"autocog.campaign.ok": False,
                  "error.message": f"no worker hosts {tag}"})
        rc = 1
        continue
    out = os.path.join(out_root, label) + ".ndjson"
    if os.path.exists(out) and os.path.getsize(out) > 0:
        print(f"[{phase_name}/{os.path.basename(out)}] exists — skipping")
        lifecycle("run.skip", label, run)
        continue
    os.makedirs(os.path.dirname(out), exist_ok=True)
    from autocog.bench.campaign import resolve_data
    cmd = [sys.executable, tool, "run", "--demo", run["demo"],
           "--syntax", run.get("syntax", "complete"),
           "--data", resolve_data(run["data"]),
           "--model", tag, "--worker", url, "--out", out]
    if run.get("limit"):
        cmd += ["--limit", str(run["limit"])]
    lifecycle("run.start", label, run)
    ok = subprocess.run(cmd, cwd=repo).returncode == 0
    lifecycle("run.end", label, run, **{"autocog.campaign.ok": ok})
    if not ok:
        rc = 1
probe_outs = [f for f in os.listdir(out_root) if f.endswith(".ndjson")]
if probe_outs:
    subprocess.run([sys.executable, tool, "analyze",
                    *[os.path.join(out_root, f) for f in probe_outs],
                    "--out", os.path.join(out_root, "adjudication")], cwd=repo)
sys.exit(rc)
EOF
        else
            python3 -m autocog bench campaign "$DESC" --phase "$PH" "${WARGS[@]}" \
                --json --json-log-file "$EVENTS" \
                || echo "!!! phase $PH reported failures"
        fi
    done <<< "$PHASE_NAMES"

    # -- archive + teardown ------------------------------------------------
    kill "${PIDS[@]}" 2>/dev/null || true
    wait 2>/dev/null || true
    trap - EXIT
    STAMP=$(date +%Y%m%d-%H%M%S)
    tar -czf "$RESULTS_PATH/$NAME-$STAMP.tar.gz" -C "$RESULTS_PATH" "$NAME"
    emit_event "{\"event.action\": \"campaign.end\", \"campaign\": \"$NAME\"}"
    echo "=== campaign $NAME done -> $RESULTS_PATH/$NAME-$STAMP.tar.gz ==="
done
