#!/usr/bin/env bash
IO_MODEL=""; io_flag=0
function check_io_model() {
    if [[ $1 == "1" ]];then IO_MODEL="IO_URING"
    elif [[ $1 == "0" ]];then IO_MODEL="EPOLL"
    else IO_MODEL="$1"; fi
}
while getopts "i:h" opt; do
    case "$opt" in "h") exit 0;; "i") io_flag=$OPTARG;; esac
done
check_io_model "$io_flag"

TEST_PORT=11523
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/test_program/bin"
cd "$BIN_DIR"

# Start the test program in the background
./test_program $io_flag > result1 2>&1 &
SERVER_PID=$!

# Wait for the "READY" signal from the server
for i in $(seq 1 50); do
    if grep -q "READY" result1 2>/dev/null; then
        break
    fi
    sleep 0.1
done

# Connect and send test data
python3 -c "
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(2)
s.connect(('127.0.0.1', $TEST_PORT))
s.sendall(b'Hello via coroutine read!')
time.sleep(1)
s.close()
"

# Wait for server to finish processing
wait $SERVER_PID 2>/dev/null
true

# Print result from result file
PASS_CNT=$(grep "RESULT     : PASS" result1 | wc -l)
echo "#####################################################"
echo "# Directory  : coroutine"
echo "# CASE       : 23"
echo "# IO MODEL   : $IO_MODEL"
if [[ $PASS_CNT -eq 1 ]]; then echo "# RESULT     : PASS"
else echo "# RESULT     : FAILED"; fi
echo "# DESCRIPTION: cs.read() on accepted TCP fd. start.sh spawns test_program and sends data via python3 client. Verifies data match."
echo "#####################################################"
