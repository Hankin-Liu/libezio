#!/usr/bin/env bash

IO_MODEL=""
io_flag=0
function print_usage()
{
    echo "Usage: sh start.sh [OPTION...]
Run current test case automatically.

USAGE: sh start.sh [-h] [-i io_flag]

  -i io_flag               IO model. 0 - epoll, 1 - io_uring.
  -h                       Give this help list"
}

function check_io_model()
{
    if [[ $1 == "1" ]];then
        IO_MODEL="IO_URING"
    elif [[ $1 == "0" ]];then
        IO_MODEL="EPOLL"
    else
        IO_MODEL="$1"
    fi
}

while getopts "i:h" opt; do
    case "$opt" in
        "h")
            print_usage
            exit 0
            ;;
        "i")
            io_flag=$OPTARG
            ;;
        ":")
            echo "Error: missing parameter value"
            exit 1 ;;
        "?")
            echo "Error: unknown option"
            exit -1
            ;;
        "*")
            echo "** abort **"
            exit -1
            ;;
    esac
done

check_io_model "$io_flag"

cd test_program/bin
./test_program $io_flag > result1 2>&1
cd -

PASS_CNT=$(grep "RESULT     : PASS" test_program/bin/result1 | wc -l)

echo "#####################################################"
echo "# Directory  : coroutine"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $PASS_CNT -eq 1 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: cs.sleep(1s,0) and cs.sleep(1ms,0) co_await. Verifies both timers fire and both coroutines resume via counter=2."
echo "#####################################################"
