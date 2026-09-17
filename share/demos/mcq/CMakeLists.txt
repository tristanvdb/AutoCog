
file(GLOB MCQ_STL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.stl)

set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/complete.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)

# --- Inspection graphs -------------------------------------------------------
# Every demo, every prompt, rendered as a graph to be READ. These are not
# assertions: nothing here knows what the automaton should look like, and a
# pass only means the pipeline produced a graph. They exist because the
# expensive defects in the last campaign were all visible in artifacts the
# pipeline already produced and nobody looked at -- that ARC choices already
# carry letters, so `select` was asking the model to translate a visible
# letter into a position; and that `stripped` is precisely the scaffold that
# drops the choice indices, which is mechanically why `select` collapsed
# under it.
#
#   ctest -L inspect                     # regenerate
#   <build>/inspect/mcq/<demo>/          # then go and read them
#
# The syntax list IS the contrast being drawn. complete / index-one /
# index-none differ from each other by exactly one knob, so any difference
# between their graphs is attributable to that knob; `special` is in for the
# special-token rendering, the likeliest place for a gap. Widen the list
# here, not by editing tests.
set(INSPECT_SYNTAXES complete index-one index-none special
    CACHE STRING "Syntaxes rendered by the mcq inspection graphs")

# A CMake list interpolates with semicolons, which `for SYN in ...` reads as
# command separators.
string(REPLACE ";" " " INSPECT_SYNTAX_ARGS "${INSPECT_SYNTAXES}")

set(INSPECT_DIR ${CMAKE_BINARY_DIR}/inspect/mcq)
set(INSPECT_CONTENT ${CMAKE_CURRENT_SOURCE_DIR}/inspect/content.json)
set(FTA2DOT ${PROJECT_SOURCE_DIR}/tools/fta2dot.py)
set(STA2DOT ${PROJECT_SOURCE_DIR}/tools/sta2dot.py)

find_program(DOT_EXECUTABLE dot)
if(NOT DOT_EXECUTABLE)
  message(STATUS "graphviz `dot` not found: inspection graphs emit .dot only")
endif()

foreach(STL_FILE ${MCQ_STL_FILES})
    get_filename_component(DEMO_NAME ${STL_FILE} NAME_WE)

    # Some files here are libraries the demos import (the shared `choice`
    # record), not programs: no `export ... as main`, so there is no entry
    # point to instantiate. The demos that import them cover them.
    file(READ ${STL_FILE} STL_SOURCE)
    string(FIND "${STL_SOURCE}" "export " HAS_EXPORT)
    if(HAS_EXPORT EQUAL -1)
        continue()
    endif()

    # Compile smoke
    add_test(
        NAME demo-stlc-mcq-${DEMO_NAME}
        COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -I ${STLIB} ${STL_FILE}
    )
    set_tests_properties(demo-stlc-mcq-${DEMO_NAME} PROPERTIES LABELS "stlc;demo")

    # Instantiation smoke: compile to STA, then instantiate the main entry prompt
    # with empty content.
    add_test(NAME demo-ista-mcq-${DEMO_NAME}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=\$(mktemp)
                $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} ${STL_FILE} 2>/dev/null
                P=\$(${Python_EXECUTABLE} -c 'import json,sys; print(json.load(open(sys.argv[1]))[\"entry_points\"][\"main\"][\"prompt\"])' \$STA)
                $<TARGET_FILE:autocog_ista> --sta \$STA --prompt \$P --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta /dev/null
                rm -f \$STA
            "
    )
    set_tests_properties(demo-ista-mcq-${DEMO_NAME} PROPERTIES LABELS "ista;demo")

    # Graphs for every prompt in the demo, every syntax in the contrast.
    #
    # Prompts are enumerated from the STA, not from the source: a prompt with
    # arguments is stored under a mangled name (`choose` becomes `choose__8`,
    # one entry per instantiation), and the source name does not resolve.
    add_test(NAME inspect-mcq-${DEMO_NAME}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                OUT=${INSPECT_DIR}/${DEMO_NAME}
                rm -rf \$OUT && mkdir -p \$OUT
                STA=\$OUT/${DEMO_NAME}.sta.json
                $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} -I ${CMAKE_CURRENT_SOURCE_DIR} ${STL_FILE} 2>/dev/null

                for P in \$(${Python_EXECUTABLE} ${STA2DOT} \$STA --list); do
                  # The schema's control structure: one per prompt, no syntax
                  # involved, arrays still rolled up.
                  ${Python_EXECUTABLE} ${STA2DOT} \$STA \$P > \$OUT/\$P.sta.dot

                  # What this prompt is fed, written out beside its graphs so
                  # the two can be read together. The manifest is keyed on the
                  # source name, so strip the mangling. No entry means {}: the
                  # schema's full shape, arrays at their maximum, rather than
                  # the one path a real question forces.
                  CONTENT=\$OUT/\$P.content.json
                  ${Python_EXECUTABLE} -c \"
import json, sys
manifest, demo, prompt, out = sys.argv[1:5]
m = json.load(open(manifest))
content = m.get(demo + '.' + prompt, m.get(demo + '.' + prompt.split('__')[0], {}))
json.dump(content, open(out, 'w'), indent=2)
\" ${INSPECT_CONTENT} ${DEMO_NAME} \$P \$CONTENT

                  for SYN in ${INSPECT_SYNTAX_ARGS}; do
                    FTA=\$OUT/\$P.\$SYN.fta.json
                    $<TARGET_FILE:autocog_ista> --sta \$STA --prompt \$P --syntax ${PROJECT_SOURCE_DIR}/share/syntax/\$SYN.json --search ${SEARCH} --content \$CONTENT --fta \$FTA 2>/dev/null
                    ${Python_EXECUTABLE} ${FTA2DOT} \$FTA > \$OUT/\$P.\$SYN.fta.dot
                  done
                done

                if [ -n \"${DOT_EXECUTABLE}\" ]; then
                  for D in \$OUT/*.dot; do
                    ${DOT_EXECUTABLE} -Tsvg \$D -o \${D%.dot}.svg
                  done
                fi
            "
    )
    set_tests_properties(inspect-mcq-${DEMO_NAME} PROPERTIES
        LABELS "inspect;demo" TIMEOUT 180)
endforeach()
