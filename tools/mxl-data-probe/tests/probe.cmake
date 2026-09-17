# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

function(fail message)
    message(FATAL_ERROR "${message}\nstdout:\n${output}\nstderr:\n${error}")
endfunction()

function(run_probe expected_status)
    execute_process(COMMAND "${PROBE}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
    if (NOT status STREQUAL "${expected_status}")
        fail("Unexpected probe exit status: ${status}, expected ${expected_status}")
    endif ()
    set(output "${output}" PARENT_SCOPE)
    set(error "${error}" PARENT_SCOPE)
endfunction()

function(expect text substring)
    string(FIND "${text}" "${substring}" position)
    if (position EQUAL -1)
        fail("Missing output: ${substring}")
    endif ()
endfunction()

set(event_id "cabbc00d-3860-4438-bc48-8ebdfe67305e")
run_probe(0 --domain "${domain}" --flow "${event_id}")
expect("${output}" "Event 0\n  timestamp: 9000 TAI ns")
expect("${output}" "  registry: SMPTE (1)\n  data item type: 010203")
expect("${output}" "  event size: 5 bytes\n  offset: 0 bytes\n  complete: 1\n  fragmentation: unfragmented")
expect("${output}" "    0000: 0x00 0x7F 0x80 0xFE 0xFF")
string(FIND "${output}" "Event 1\n" extra_event)
if (NOT extra_event EQUAL -1)
    fail("Default count must read exactly one event")
endif ()

# URI input and count apply to event entries, including each fragment.
run_probe(0 "mxl://${domain}?id=${event_id}" --count 5)
expect("${output}" "Event 1\n  timestamp: 10000 TAI ns")
expect("${output}" "  event size: 5 bytes\n  offset: 0 bytes\n  complete: 0\n  fragmentation: first fragment")
expect("${output}" "Event 2\n  timestamp: 10000 TAI ns")
expect("${output}" "  event size: 3 bytes\n  offset: 5 bytes\n  complete: 1\n  fragmentation: last fragment")
expect("${output}" "Event 3\n  timestamp: 10001 TAI ns")
expect("${output}" "  registry: DMF MXL (0)\n  data item type: x-mxl:empty")
expect("${output}" "  payload: <empty>")
string(REPEAT "X" 256 long_type)
expect("${output}" "  data item type: ${long_type}\n  event size: 1 bytes")

run_probe(1 --domain "${domain}" --flow "${event_id}" --count 6 --timeout-ms 0)
expect("${error}" "Failed to read event 5: MXL_ERR_OUT_OF_RANGE_TOO_EARLY")
run_probe(1 --domain "${domain}" --flow "cabbc00d-3860-4438-bc48-8ebdfe67305f" --timeout-ms 1)
expect("${error}" "Failed to read event 0: MXL_ERR_OUT_OF_RANGE_TOO_EARLY")

# ANC still starts at the current head, rather than the oldest grain.
run_probe(0 --domain "${domain}" --flow "db3bd465-2772-484f-8fac-830b0471258b")
expect("${output}" "Grain 42\n")
expect("${output}" "  RFC-8331 length: 0 bytes\n  ANC count: 0\n  No ANC elements")
