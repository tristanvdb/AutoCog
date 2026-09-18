# The shipped syntax configs.
#
# A campaign's own syntaxes are NOT here: they are a base file plus
# overrides, declared in its descriptor and resolved into
# results/<campaign>/syntax/ (share/campaign/syntaxes.py). This directory
# holds the bases they override -- eleven files, not a generated product of
# every knob combination.

set(SYNTAX_DIR ${PROJECT_SOURCE_DIR}/share/syntax)
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(DEMOS ${PROJECT_SOURCE_DIR}/share/demos/mcq)

# Every shipped syntax validates, and actually instantiates. A syntax that
# parses but cannot render a prompt is a campaign that dies on the box
# after the models are loaded.
add_test(
    NAME syntax_all_instantiate
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            D=$(mktemp -d)
            $<TARGET_FILE:autocog_stlc> --sta \$D/sta -I ${STLIB} -I ${DEMOS} ${DEMOS}/record-value.stl 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
json.dump(json.load(open(sys.argv[1]))['record-value.main'], open(sys.argv[2],'w'))
\" ${DEMOS}/inspect/content.json \$D/content.json
            n=0
            for SYN in ${SYNTAX_DIR}/*.json; do
              $<TARGET_FILE:autocog_ista> --sta \$D/sta --prompt main --syntax \$SYN --search ${SEARCH} --content \$D/content.json --fta /dev/null 2>\$D/err \
                || { echo \"FAILED to instantiate under \$(basename \$SYN):\"; cat \$D/err; exit 1; }
              n=\$((n+1))
            done
            echo \"OK: \$n syntaxes instantiate\"
            test \$n -ge 10
            rm -rf \$D
        "
)
set_tests_properties(syntax_all_instantiate PROPERTIES LABELS "syntax" TIMEOUT 300)

# dedup.py measures which of a DESCRIPTOR's syntaxes render differently for
# each of its programs. The fixture varies one knob that bites and one that
# cannot: prompt_indent emits a prefix per nesting level, so on the flat
# strings-* schema its levels collapse into one, while the index base
# changes what `select` offers and does not.
#
# This is the check that stops a campaign paying twice for one measurement,
# so the thing under test is that it still SEES a collapse -- a dedup pass
# that reported everything distinct would be silently useless.
add_test(
    NAME syntax_dedup_finds_the_collapse
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            D=$(mktemp -d)
            cat > \$D/probe.yml <<'EOF'
models: [{tag: rng, file: rng}]
datasets: [{name: q, samples: 1}]
programs:
  strings-select: {filepath: demos/mcq/strings-select.stl}
syntaxes:
  i0-flat: {filepath: complete.json, prompt_zero_index: true,  prompt_indent: \"\"}
  i0-gt:   {filepath: complete.json, prompt_zero_index: true,  prompt_indent: \"> \"}
  i1-flat: {filepath: complete.json, prompt_zero_index: false, prompt_indent: \"\"}
runs:
  - {name: r, model: rng, dataset: q, programs: [strings-select],
     syntaxes: [i0-flat, i0-gt, i1-flat]}
EOF
            ${Python_EXECUTABLE} ${SYNTAX_DIR}/dedup.py \$D/probe.yml > \$D/out
            cat \$D/out
            grep -q 'strings-select *2 / 3' \$D/out
            grep -q 'i0-flat, i0-gt' \$D/out
            rm -rf \$D
        "
)
set_tests_properties(syntax_dedup_finds_the_collapse PROPERTIES
    LABELS "syntax" TIMEOUT 300)
