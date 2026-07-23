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
cd test_program/bin
./test_program $io_flag > result1 2>&1
true
cd -
PASS_CNT=$(grep "RESULT     : PASS" test_program/bin/result1 | wc -l)
echo "#####################################################"
echo "# Directory  : coroutine"
echo "# CASE       : 9"
echo "# IO MODEL   : $IO_MODEL"
if [[ $PASS_CNT -eq 1 ]]; then echo "# RESULT     : PASS"
else echo "# RESULT     : FAILED"; fi
echo "# DESCRIPTION: task<T> chaining: outer awaits inner task. Value propagation."
echo "#####################################################"
