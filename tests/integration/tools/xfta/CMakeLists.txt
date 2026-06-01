# xfta integration tests

set(FTA_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/fta)

# Tokenized FTA fixture tests — output FTT JSON to stdout
add_test(NAME xfta_text_completion
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 ${FTA_FIXTURES}/test_text_completion.json > /dev/null")
set_tests_properties(xfta_text_completion PROPERTIES LABELS "xfta")

add_test(NAME xfta_text_choice
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 ${FTA_FIXTURES}/test_text_choice.json > /dev/null")
set_tests_properties(xfta_text_choice PROPERTIES LABELS "xfta")

# CLI argument tests
add_test(NAME xfta_args_verbose
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 -v ${FTA_FIXTURES}/test_text_choice.json > /dev/null")
set_tests_properties(xfta_args_verbose PROPERTIES LABELS "xfta;args")

# Output to file
add_test(NAME xfta_args_output_file
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=$(mktemp) && $<TARGET_FILE:autocog_xfta> --rng --seed 42 -o \$TMPF ${FTA_FIXTURES}/test_text_choice.json && test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(xfta_args_output_file PROPERTIES LABELS "xfta;args")

# Multiple input files
add_test(NAME xfta_args_multi_input
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 ${FTA_FIXTURES}/test_text_completion.json ${FTA_FIXTURES}/test_text_choice.json > /dev/null")
set_tests_properties(xfta_args_multi_input PROPERTIES LABELS "xfta;args")

# Vocab mask enforcement: tokenize("0".."9") must constrain the completion to
# digits only. Asserts the generated completion text is non-empty and all digits
# (RNG model, byte-level vocab; deterministic with --seed).
add_test(NAME xfta_vocab_digits
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 7 ${FTA_FIXTURES}/test_vocab_digits.json | grep -v libgcov | ${Python_EXECUTABLE} -c \"
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
