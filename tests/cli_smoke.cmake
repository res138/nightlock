# End-to-end smoke test of the CLI, driven through --password-stdin.
# Invoked by ctest:
#   cmake -DNIGHTLOCK_CLI=<binary> -DWORK_DIR=<dir> -P cli_smoke.cmake

if(NOT NIGHTLOCK_CLI OR NOT WORK_DIR)
    message(FATAL_ERROR "NIGHTLOCK_CLI and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(VAULT "${WORK_DIR}/smoke.nlck")

function(run_nl expected_code input)
    file(WRITE "${WORK_DIR}/stdin.txt" "${input}")
    execute_process(
        COMMAND ${NIGHTLOCK_CLI} -f ${VAULT} --password-stdin ${ARGN}
        INPUT_FILE "${WORK_DIR}/stdin.txt"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        RESULT_VARIABLE code)
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR
            "nightlock ${ARGN}: expected exit ${expected_code}, got ${code}\n"
            "stdout: ${out}\nstderr: ${err}")
    endif()
    set(NL_OUTPUT "${out}" PARENT_SCOPE)
endfunction()

run_nl(0 "master-pw\n" init)
run_nl(0 "master-pw\n" mkdir "Personal/Email")
run_nl(0 "master-pw\nhunter2-secret\n" add "Personal/Email/Gmail" --login me@example.com)
run_nl(0 "master-pw\n" add "Personal/Generated" --gen --length 30 --symbols)

run_nl(0 "master-pw\n" ls)
if(NOT NL_OUTPUT MATCHES "Gmail" OR NOT NL_OUTPUT MATCHES "Email/")
    message(FATAL_ERROR "ls is missing expected rows:\n${NL_OUTPUT}")
endif()

run_nl(0 "master-pw\n" show "Personal/Email/Gmail")
if(NL_OUTPUT MATCHES "hunter2-secret")
    message(FATAL_ERROR "masked show leaked the password:\n${NL_OUTPUT}")
endif()

run_nl(0 "master-pw\n" show "Personal/Email/Gmail" -p)
if(NOT NL_OUTPUT MATCHES "hunter2-secret")
    message(FATAL_ERROR "show -p did not reveal the password:\n${NL_OUTPUT}")
endif()

# The raw image must carry the magic and never the plaintext secret.
file(READ "${VAULT}" raw HEX)
string(FIND "${raw}" "4e4c434b" magic_at)
if(NOT magic_at EQUAL 0)
    message(FATAL_ERROR "vault file does not start with NLCK magic")
endif()
string(HEX "hunter2-secret" secret_hex)
string(FIND "${raw}" "${secret_hex}" leak_at)
if(NOT leak_at EQUAL -1)
    message(FATAL_ERROR "plaintext secret found inside the vault file")
endif()

run_nl(2 "wrong-pw\n" ls)

run_nl(0 "master-pw\nnew-master\n" passwd)
run_nl(2 "master-pw\n" ls)
run_nl(0 "new-master\n" ls)

run_nl(0 "new-master\n" rm "Personal/Email/Gmail")
run_nl(0 "new-master\n" rm "Personal/Email")
run_nl(1 "new-master\n" rm "Personal")  # non-empty folder refuses

# Tamper: flip one ciphertext byte -> authentication failure (exit 2).
file(READ "${VAULT}" raw HEX)
string(LENGTH "${raw}" hexlen)
math(EXPR flip_at "200 * 2")
string(SUBSTRING "${raw}" ${flip_at} 2 byte)
if(byte STREQUAL "00")
    set(byte "ff")
else()
    set(byte "00")
endif()
string(SUBSTRING "${raw}" 0 ${flip_at} head)
math(EXPR tail_at "${flip_at} + 2")
math(EXPR tail_len "${hexlen} - ${tail_at}")
string(SUBSTRING "${raw}" ${tail_at} ${tail_len} tail)
set(VAULT "${WORK_DIR}/tampered.nlck")
string(CONCAT tampered "${head}" "${byte}" "${tail}")
# file(WRITE ... HEX) does not exist; go through a generated file.
set(hexfile "${WORK_DIR}/tampered.hex")
file(WRITE "${hexfile}" "${tampered}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python3 -c
        "import sys,binascii;open(sys.argv[2],'wb').write(binascii.unhexlify(open(sys.argv[1]).read().strip()))"
        "${hexfile}" "${VAULT}"
    RESULT_VARIABLE hexcode)
if(NOT hexcode EQUAL 0)
    message(FATAL_ERROR "could not materialize the tampered vault")
endif()
run_nl(2 "new-master\n" ls)

message(STATUS "cli smoke: all checks passed")
