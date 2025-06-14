#!/usr/bin/env bash

IO_MODEL=""
io_flag=0  # use epoll by default
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
            echo "Error: missing parameter value: ${OptString}, index is: $OPTIND"
            print_usage
            exit 1 ;;
        "?")
            echo "Error: unknown option: ${OptString}, index is: $OPTIND"
            print_usage
            exit -1
            ;;
        "*")
            echo "** abort **"
            print_usage
            exit -1
            ;;
    esac
done

check_io_model "$io_flag"

cd test_program/bin
./test_program $io_flag > result1 &

sleep 31
RESULT_CNT1=$(grep 'timer 1' result1 | wc -l)
echo "RESULT_CNT1 = $RESULT_CNT1"
RESULT_CNT2=$(ps -ef | grep test_program | grep -v 'grep' |  wc -l)
echo "RESULT_CNT2 = $RESULT_CNT2"
RESULT_CNT3=$(grep 'timer 2' result1 | wc -l)
echo "RESULT_CNT3 = $RESULT_CNT3"
cd -

killall -9 test_program

echo "#####################################################"
echo "# Directory  : timer"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 20 && $RESULT_CNT2 -eq 1 && $RESULT_CNT3 -eq 20 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Create 2 timers. Timer 1 is triggered 1 time per second. Timer 2 is triggered 1 time per 1.5 seconds. After triggered 20 times, cancel these 2 timers."
echo "#####################################################"
