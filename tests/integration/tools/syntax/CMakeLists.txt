# The shipped syntax configs, and the generated grid.
#
# The grid (share/syntax/grid) is 64 cells over four rendering knobs, written
# by share/syntax/generate.py. Checked in because a syntax is data a campaign
# names, and generated because 64 near-identical JSON files drift the moment
# one is edited by hand.

set(SYNTAX_DIR ${PROJECT_SOURCE_DIR}/share/syntax)
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(DEMOS ${PROJECT_SOURCE_DIR}/share/demos/mcq)

# The checked-in grid is what the spec says it is.
add_test(NAME syntax_grid_matches_spec
         COMMAND ${Python_EXECUTABLE} ${SYNTAX_DIR}/generate.py --check)
set_tests_properties(syntax_grid_matches_spec PROPERTIES LABELS "syntax")

# Every syntax -- shipped and grid -- validates, and actually instantiates.
# A syntax that parses but cannot render a prompt is a campaign that dies on
# the box after the models are loaded.
file(GLOB SHIPPED_SYNTAXES ${SYNTAX_DIR}/*.json)
file(GLOB GRID_SYNTAXES ${SYNTAX_DIR}/grid/*.json)

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
            for SYN in ${SYNTAX_DIR}/*.json ${SYNTAX_DIR}/grid/*.json; do
              $<TARGET_FILE:autocog_ista> --sta \$D/sta --prompt main --syntax \$SYN --search ${SEARCH} --content \$D/content.json --fta /dev/null 2>\$D/err \
                || { echo \"FAILED to instantiate under \$(basename \$SYN):\"; cat \$D/err; exit 1; }
              n=\$((n+1))
            done
            echo \"OK: \$n syntaxes instantiate\"
            test \$n -ge 70
            rm -rf \$D
        "
)
set_tests_properties(syntax_all_instantiate PROPERTIES LABELS "syntax" TIMEOUT 300)

# The grid is a CONTRAST, so no two cells may render the same thing for a
# program that exercises every axis. record-index is nested and commits by
# index, so all four index levels and all four indent levels bite; if two
# cells collapse there, an axis is not doing what its name claims and the
# campaign would pay twice for one measurement.
#
# The same check on a FLAT program is expected to collapse (indent is a no-op
# without nesting) and is what the campaign's dedup pass is for -- that
# reduction is measured per program, not assumed here.
add_test(
    NAME syntax_grid_cells_are_distinct
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            D=$(mktemp -d)
            $<TARGET_FILE:autocog_stlc> --sta \$D/sta -I ${STLIB} -I ${DEMOS} ${DEMOS}/record-index.stl 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
json.dump(json.load(open(sys.argv[1]))['record-index.main'], open(sys.argv[2],'w'))
\" ${DEMOS}/inspect/content.json \$D/content.json
            for SYN in ${SYNTAX_DIR}/grid/*.json; do
              $<TARGET_FILE:autocog_ista> --sta \$D/sta --prompt main --syntax \$SYN --search ${SEARCH} --content \$D/content.json --fta \$D/\$(basename \$SYN .json).fta 2>/dev/null
            done
            ${Python_EXECUTABLE} -c \"
import glob, hashlib, json, os, sys, collections
seen = collections.defaultdict(list)
for p in sorted(glob.glob('\$D/*.fta')):
    acts = json.load(open(p))['actions']
    seen[hashlib.sha256(json.dumps(acts, sort_keys=True).encode()).hexdigest()].append(
        os.path.basename(p)[:-4])
dups = {k: v for k, v in seen.items() if len(v) > 1}
if dups:
    print('grid cells that render identically on a nested, index-committing program:')
    for v in dups.values():
        print('   ', ', '.join(v))
    sys.exit(1)
print('OK:', len(seen), 'distinct renderings from', sum(len(v) for v in seen.values()), 'cells')
\"
            rm -rf \$D
        "
)
set_tests_properties(syntax_grid_cells_are_distinct PROPERTIES
    LABELS "syntax" TIMEOUT 300)

# The size of the real search space, over the programs a campaign uses.
# 64 cells x 9 programs names 576 arms and only 332 of them are distinct
# renderings -- the rest are the same measurement bought twice.
#
# Pinned, because the number is what a campaign is SIZED from: if a program
# or a knob changes it, the descriptor generated from the old number is
# quietly wrong (an arm too many is money, an arm too few is a hole in every
# comparison). Regenerate the descriptor and update the pin together.
add_test(NAME syntax_search_space_is_pinned
         COMMAND ${Python_EXECUTABLE} ${SYNTAX_DIR}/dedup.py --expect 332)
set_tests_properties(syntax_search_space_is_pinned PROPERTIES
    LABELS "syntax" TIMEOUT 300)
