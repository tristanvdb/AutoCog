# FTA simplification: collapsing a fully-determined FTA for finetuning export.
#
#     stlc -> ista(content=frame, --simplify MODE) -> xfta --rng
#
# The property that has to hold is that simplification changes the SHAPE and
# not the TEXT: whatever the mode, the rendered prompt is the one generation
# produced, because a finetuning corpus that drifts from the runtime's
# rendering is training on prompts the runtime will never send.
#
# What it does change is the tokenization. Merging two text actions lets the
# tokenizer merge across the seam: on test_array_fields under Llama-3 BPE the
# unsimplified path is 473 tokens and the fully simplified one is 472. That is
# the point for finetuning (the natural tokenization of the whole text is what
# a language model sees) and it is why scoring a simplified FTA is refused —
# friction is about the tokens the runtime actually forces. Both directions are
# asserted below.

set(E2E_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/e2e/golden)
set(STL_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/complete.json)
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)

file(GLOB_RECURSE GOLDEN_FILES "${E2E_GOLDEN}/*.golden.json")

# One test per golden, all three modes compared in a single invocation.
foreach(golden_path ${GOLDEN_FILES})
  file(RELATIVE_PATH golden_rel "${E2E_GOLDEN}" "${golden_path}")
  get_filename_component(golden_dir "${golden_rel}" DIRECTORY)
  get_filename_component(golden_file "${golden_rel}" NAME_WLE)
  string(REGEX REPLACE "\\.golden$" "" base_prompt "${golden_file}")
  string(REGEX MATCH "^(.+)\\.([^.]+)$" _ "${base_prompt}")
  set(test_name "${CMAKE_MATCH_1}")
  set(prompt_name "${CMAKE_MATCH_2}")
  set(stl_path "${STL_FIXTURES}/${golden_dir}/${test_name}.stl")
  string(REPLACE "/" "_" test_label "${golden_dir}/${test_name}.${prompt_name}")

  add_test(
      NAME "simplify_${test_label}"
      COMMAND ${CMAKE_COMMAND} -E env
          bash -c "
              set -e
              D=$(mktemp -d)
              STL_DIR=$(dirname ${stl_path})
              $<TARGET_FILE:autocog_stlc> --sta \$D/sta -I ${STLIB} -I \$STL_DIR ${stl_path} 2>/dev/null
              for mode in none limited full; do
                $<TARGET_FILE:autocog_ista> --sta \$D/sta --prompt ${prompt_name} --syntax ${SYNTAX} --search ${SEARCH} --content ${golden_path} --simplify \$mode --fta \$D/fta.\$mode 2>/dev/null
                $<TARGET_FILE:autocog_xfta> --fta \$D/fta.\$mode --rng --ftt \$D/ftt.\$mode 2>/dev/null
              done
              ${Python_EXECUTABLE} -c \"
import json, sys
def spine(p):
    n, text = json.load(open(p)), ''
    while True:
        text += n['text']
        kids = n['children']
        if not kids: return text
        n = kids[0]
def actions(p):
    return json.load(open(p))['actions']

text = {m: spine('\$D/ftt.' + m) for m in ('none', 'limited', 'full')}
if text['none'] != text['limited'] or text['none'] != text['full']:
    print('SIMPLIFICATION CHANGED THE RENDERING'); sys.exit(1)

none, limited, full = (actions('\$D/fta.' + m) for m in ('none', 'limited', 'full'))
if len(full) != 1:
    print('full is', len(full), 'actions, expected 1'); sys.exit(1)
if not (len(full) <= len(limited) <= len(none)):
    print('limited should sit between full and none'); sys.exit(1)

# Limited keeps a boundary at EVERY value and merges the structure between
# them. Preserving the value count exactly is the property a weighted
# finetuning loss depends on: one segment per field occurrence, no more and
# no fewer, or the weights land on the wrong tokens.
def values(acts):
    return [(a.get('field'), a.get('indices')) for a in acts
            if a.get('field') is not None]
if values(limited) != values(none):
    print('limited lost value boundaries:',
          len(values(limited)), 'vs', len(values(none))); sys.exit(1)
for a, b in zip(limited, limited[1:]):
    if (a.get('field'), a.get('indices')) == (b.get('field'), b.get('indices')):
        print('limited left an unmerged seam at', a['uid']); sys.exit(1)
for a in limited:
    if a.get('field') is not None and not a['uid'].startswith('field.'):
        print('a value segment lost its STA uid:', a['uid']); sys.exit(1)

for mode, acts in (('limited', limited), ('full', full)):
    for i, a in enumerate(acts):
        want = [acts[i + 1]['uid']] if i + 1 < len(acts) else []
        if a['successors'] != want:
            print(mode, 'is not a chain at', a['uid']); sys.exit(1)
\"
              rm -rf \$D
          "
  )
  set_tests_properties("simplify_${test_label}" PROPERTIES
      LABELS "simplify" TIMEOUT 30)
endforeach()

set(SC_STL ${STL_FIXTURES}/language/structures/test_array_fields.stl)
set(SC_GOLDEN ${E2E_GOLDEN}/language/structures/test_array_fields.collector.golden.json)

# A partly determined FTA still simplifies: decisions survive as boundaries and
# the fixed text between them is gathered up. This is what makes an FTA
# readable -- the mcq demos drop from 25 actions to 5 -- and it is only safe
# because the merge never crosses a branch. The invariant is that the SET OF
# PATHS through the automaton is unchanged, text for text: merging two actions
# requires the first to go nowhere else and nothing else to enter the second,
# so paths are in bijection and no route gains, loses or reorders a character.
add_test(
    NAME simplify_preserves_every_path
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            D=$(mktemp -d)
            DEMOS=${PROJECT_SOURCE_DIR}/share/demos/mcq
            $<TARGET_FILE:autocog_stlc> --sta \$D/sta -I ${STLIB} -I \$DEMOS \$DEMOS/strings-select.stl 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
json.dump(json.load(open(sys.argv[1]))['strings-select.main'], open(sys.argv[2], 'w'))
\" \$DEMOS/inspect/content.json \$D/determined.json
            echo '{}' > \$D/open.json
            for FIXTURE in determined open; do
              for MODE in none limited full; do
                $<TARGET_FILE:autocog_ista> --sta \$D/sta --prompt main --syntax ${SYNTAX} --search ${SEARCH} --content \$D/\$FIXTURE.json --simplify \$MODE --fta \$D/\$FIXTURE.\$MODE.fta 2>/dev/null
              done
            done
            ${Python_EXECUTABLE} -c \"
import json, sys

def paths(path):
    fta = json.load(open(path))
    by_uid = {a['uid']: a for a in fta['actions']}
    out = []

    def walk(action, prefix, depth):
        if depth > 400:
            raise SystemExit('cycle or runaway in ' + path)
        kind = action['type']
        if kind == 'text':
            text = prefix + action['text']
        elif kind == 'complete':
            # Generated: the text is not known statically, but the action is
            # never merged, so its identity must survive.
            text = prefix + '<complete:' + action['uid'] + '>'
        else:
            text = prefix
        succ = action['successors']
        if not succ:
            out.append(text); return
        if kind == 'choose':
            choices = action['choices']
            if not choices:
                # A choose with no candidates: instantiating without content
                # leaves a select with nothing to select from. Not evaluable,
                # but legal for inspection, and it still has a continuation.
                walk(by_uid[succ[0]], text, depth + 1)
                return
            # Successors are index-aligned with candidates when there is one
            # per candidate; a value choose has a single continuation that
            # every candidate shares.
            for i, choice in enumerate(choices):
                nxt = succ[i] if len(succ) == len(choices) else succ[0]
                walk(by_uid[nxt], text + str(choice), depth + 1)
        else:
            for s in succ:
                walk(by_uid[s], text, depth + 1)

    walk(fta['actions'][0], '', 0)
    return sorted(out), len(fta['actions'])

for fixture, why in (('determined', 'a forced path'),
                     ('open', 'branches and a join')):
    base, n_base = paths('\$D/' + fixture + '.none.fta')
    if len(base) < 2:
        print(fixture, 'has a single path: it cannot show branch survival')
        sys.exit(1)
    for mode in ('limited', 'full'):
        got, n = paths('\$D/' + fixture + '.' + mode + '.fta')
        if got != base:
            print(fixture, mode, 'changed the paths:', len(got), 'vs', len(base))
            for a, b in zip(got, base):
                if a != b:
                    print(' got :', repr(a[:200]))
                    print(' want:', repr(b[:200]))
                    break
            sys.exit(1)
        if n >= n_base:
            print(fixture, mode, 'did not reduce the count:', n, '>=', n_base)
            sys.exit(1)
        print('OK %-11s %-8s %2d actions (from %2d), %d paths unchanged (%s)'
              % (fixture, mode, n, n_base, len(got), why))
\"
            rm -rf \$D
        "
)
set_tests_properties(simplify_preserves_every_path PROPERTIES
    LABELS "simplify" TIMEOUT 60)

# The two must never be combined: simplification is for finetuning export,
# scoring is for friction, and they disagree about what the token stream is.
add_test(
    NAME simplify_cannot_be_scored
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            D=$(mktemp -d)
            $<TARGET_FILE:autocog_stlc> --sta \$D/sta -I ${STLIB} ${SC_STL} 2>/dev/null
            for mode in limited full; do
              $<TARGET_FILE:autocog_ista> --sta \$D/sta --prompt collector --syntax ${SYNTAX} --search ${SEARCH} --content ${SC_GOLDEN} --simplify \$mode --fta \$D/fta 2>/dev/null
              if $<TARGET_FILE:autocog_xfta> --fta \$D/fta --rng --score-all --ftt /dev/null 2>\$D/err; then
                  echo \"scored a \$mode-simplified FTA\"; exit 1
              fi
              grep -q 'Refusing to score' \$D/err || { echo 'wrong error:'; cat \$D/err; exit 1; }
              # Evaluating it WITHOUT scoring stays legal: that is how the
              # finetuning token list is produced.
              $<TARGET_FILE:autocog_xfta> --fta \$D/fta --rng --ftt /dev/null 2>/dev/null
            done
            rm -rf \$D
        "
)
set_tests_properties(simplify_cannot_be_scored PROPERTIES
    LABELS "simplify;score" TIMEOUT 30)
