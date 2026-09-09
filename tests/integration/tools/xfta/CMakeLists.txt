# xfta integration tests

set(FTA_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/fta)

# Tokenized FTA fixture tests — evaluate to an FTT (discarded).
add_test(NAME xfta_text_completion
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --seed 42
             --fta ${FTA_FIXTURES}/test_text_completion.json --ftt /dev/null)
set_tests_properties(xfta_text_completion PROPERTIES LABELS "xfta")

add_test(NAME xfta_text_choice
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --seed 42
             --fta ${FTA_FIXTURES}/test_text_choice.json --ftt /dev/null)
set_tests_properties(xfta_text_choice PROPERTIES LABELS "xfta")

# CLI argument tests
add_test(NAME xfta_args_verbose
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --seed 42 --verbose
             --fta ${FTA_FIXTURES}/test_text_choice.json --ftt /dev/null)
set_tests_properties(xfta_args_verbose PROPERTIES LABELS "xfta;args")

# --ftt writes the FTT to a file
add_test(NAME xfta_args_output_file
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             TMPF=\$(mktemp) &&
             $<TARGET_FILE:autocog_xfta> --rng --seed 42 --fta ${FTA_FIXTURES}/test_text_choice.json --ftt \$TMPF &&
             test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(xfta_args_output_file PROPERTIES LABELS "xfta;args")

# Vocab mask enforcement: tokenize("0".."9") must constrain the completion to
# digits only. Asserts the generated completion text is non-empty and all digits
# (RNG model, byte-level vocab; deterministic with --seed).
add_test(NAME xfta_vocab_digits
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 7 --fta ${FTA_FIXTURES}/test_vocab_digits.json --ftt /dev/stdout | grep -v libgcov | ${Python_EXECUTABLE} -c \"
import json, sys, re
d = json.load(sys.stdin)
outs = []
def walk(o):
    if isinstance(o, dict):
        t = o.get('text')
        if isinstance(t, str) and t not in ('PIN: ', ''): outs.append(t)
        for v in o.values(): walk(v)
    elif isinstance(o, list):
        for v in o: walk(v)
walk(d)
joined = ''.join(outs)
if not joined or not re.fullmatch(r'[0-9]+', joined):
    print('VOCAB MASK NOT ENFORCED: completion =', repr(joined)); sys.exit(1)
print('OK: completion constrained to digits:', joined)
\"")
set_tests_properties(xfta_vocab_digits PROPERTIES LABELS "xfta;vocab")

# topk vs beams: `topk` bounds the candidates sampled per beam expansion,
# `beams` the surviving hypotheses. With topk=1 a beam search of any width
# degenerates to greedy — the completion grows exactly one child. Without
# topk the historical tie (topk == beams == 4) fans out to several children.
add_test(NAME xfta_topk_narrow
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 7 --fta ${FTA_FIXTURES}/test_topk_narrow.json --ftt /dev/stdout | grep -v libgcov | ${Python_EXECUTABLE} -c \"
import json, sys
d = json.load(sys.stdin)
n = 0
def walk(o):
    global n
    if isinstance(o, dict):
        if o.get('uid') == 'response': n += 1
        for v in o.values(): walk(v)
    elif isinstance(o, list):
        for v in o: walk(v)
walk(d)
if n != 1:
    print('topk=1 should force greedy: expected 1 completion child, got', n); sys.exit(1)
print('OK: topk=1 degenerates beams=4 to greedy (1 child)')
\"")
set_tests_properties(xfta_topk_narrow PROPERTIES LABELS "xfta;search")

add_test(NAME xfta_topk_wide
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 7 --fta ${FTA_FIXTURES}/test_topk_wide.json --ftt /dev/stdout | grep -v libgcov | ${Python_EXECUTABLE} -c \"
import json, sys
d = json.load(sys.stdin)
n = 0
def walk(o):
    global n
    if isinstance(o, dict):
        if o.get('uid') == 'response': n += 1
        for v in o.values(): walk(v)
    elif isinstance(o, list):
        for v in o: walk(v)
walk(d)
if n < 2:
    print('implicit topk==beams should fan out: expected >=2 completion children, got', n); sys.exit(1)
print('OK: beams=4 without topk fans out to', n, 'children')
\"")
set_tests_properties(xfta_topk_wide PROPERTIES LABELS "xfta;search")

# Queue ordering: a lexicographic metric list evaluates cleanly; an unknown
# metric name is rejected when the evaluation is created.
add_test(NAME xfta_queue_metric
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --seed 42
             --fta ${FTA_FIXTURES}/test_queue_metric.json --ftt /dev/null)
set_tests_properties(xfta_queue_metric PROPERTIES LABELS "xfta;queue")

add_test(NAME xfta_queue_metric_unknown
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --seed 42
             --fta ${FTA_FIXTURES}/test_queue_metric_unknown.json --ftt /dev/null)
set_tests_properties(xfta_queue_metric_unknown PROPERTIES WILL_FAIL TRUE LABELS "xfta;queue")

# Error paths (must exit non-zero)
add_test(NAME xfta_err_missing_fta
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --ftt /dev/null)
set_tests_properties(xfta_err_missing_fta PROPERTIES WILL_FAIL TRUE LABELS "xfta;args")

add_test(NAME xfta_err_missing_ftt
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --fta ${FTA_FIXTURES}/test_text_choice.json)
set_tests_properties(xfta_err_missing_ftt PROPERTIES WILL_FAIL TRUE LABELS "xfta;args")

add_test(NAME xfta_err_no_model
         COMMAND $<TARGET_FILE:autocog_xfta> --fta ${FTA_FIXTURES}/test_text_choice.json --ftt /dev/null)
set_tests_properties(xfta_err_no_model PROPERTIES WILL_FAIL TRUE LABELS "xfta;args")
