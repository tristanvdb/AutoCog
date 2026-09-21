# The shipped syntax configs.
#
# These are DATA the runtime loads, so they are gated here. A campaign's own
# syntaxes are not: they are a base file plus overrides declared in a
# descriptor, and both the resolver and the dedup pass over them belong to
# the benchmarking harness (share/experiments), which carries its own tests.

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
