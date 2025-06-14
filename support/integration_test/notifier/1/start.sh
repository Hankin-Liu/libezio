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
stdbuf -oL ./test_program $io_flag > result1 &

sleep 25
RESULT_CNT1=$(grep 'Producer : 2000 times' result1 | wc -l)
echo "RESULT_CNT1 = $RESULT_CNT1"
RESULT_CNT2=$(grep -R 'Producer :.*times' result1 | wc -l)
echo "RESULT_CNT2 = $RESULT_CNT2"

RESULT_CNT3=$(grep 'Consumer 1 : 2000 times' result1 | wc -l)
echo "RESULT_CNT3 = $RESULT_CNT3"
RESULT_CNT4=$(grep -R 'Consumer 1 :.*times' result1 | wc -l)
echo "RESULT_CNT4 = $RESULT_CNT4"

RESULT_CNT5=$(grep 'Consumer 2 : 2000 times' result1 | wc -l)
echo "RESULT_CNT5 = $RESULT_CNT5"
RESULT_CNT6=$(grep -R 'Consumer 2 :.*times' result1 | wc -l)
echo "RESULT_CNT6 = $RESULT_CNT6"

RESULT_CNT7=$(grep 'Consumer 3 : 2000 times' result1 | wc -l)
echo "RESULT_CNT7 = $RESULT_CNT7"
RESULT_CNT8=$(grep -R 'Consumer 3 :.*times' result1 | wc -l)
echo "RESULT_CNT8 = $RESULT_CNT8"

RESULT_CNT9=$(grep 'Consumer 4 : 2000 times' result1 | wc -l)
echo "RESULT_CNT9 = $RESULT_CNT9"
RESULT_CNT10=$(grep -R 'Consumer 4 :.*times' result1 | wc -l)
echo "RESULT_CNT10 = $RESULT_CNT10"

RESULT_CNT11=$(grep 'Consumer 5 : 2000 times' result1 | wc -l)
echo "RESULT_CNT11 = $RESULT_CNT11"
RESULT_CNT12=$(grep -R 'Consumer 5 :.*times' result1 | wc -l)
echo "RESULT_CNT12 = $RESULT_CNT12"
cd -

killall -9 test_program

echo "#####################################################"
echo "# Directory  : notifier"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 1 && $RESULT_CNT2 -eq 10 && $RESULT_CNT3 -eq 1 && $RESULT_CNT4 -eq 10 && $RESULT_CNT5 -eq 1 && $RESULT_CNT6 -eq 10 && $RESULT_CNT7 -eq 1 && $RESULT_CNT8 -eq 10 && $RESULT_CNT9 -eq 1 && $RESULT_CNT10 -eq 10 && $RESULT_CNT11 -eq 1 && $RESULT_CNT12 -eq 10 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Create 1 producer thread and 5 consumer threads. Create 5 notifiers which are used by producer thread to notify consumer threads. Notify 2000 times totally. Producer notifies consumers once every 10ms. All threads print a message per 2000 notifications."
echo "#####################################################"
