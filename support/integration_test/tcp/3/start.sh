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
#stdbuf -oL ./test_program $io_flag > result1 &
./test_program $io_flag > result1 &
sleep 1

port=13579
for ((i=0;i<200;++i))
do
    nc 0.0.0.0 $port &
    sleep 0.02
done

sleep 2
killall -9 nc

RESULT_CNT1=$(cat result1 | wc -l )
echo "RESULT_CNT1 = $RESULT_CNT1"
RESULT_CNT2="1"
while IFS= read -r line; do
    RET=$(echo "$line" | awk -F 'ret = ' '{print $2}' | awk -F ',' '{print $1}')
    if [[ $RET -le 0 ]];then
        echo "ret = $RET, which is <= 0"
        RESULT_CNT2=$RET
    fi
done < result1
echo "RESULT_CNT2 = $RESULT_CNT2"

killall -9 test_program

echo "#####################################################"
echo "# Directory  : tcp"
echo "# CASE       : 3"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 200 && $RESULT_CNT2 -gt 0 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Test submit_async_accept interface. test_program create 1 listen fd on ip 0.0.0.0 and port 13579. Then calls submit_async_accept interface to accept connections, After start test_program, start 200 tcp connections one by one every 20ms on this ip and port using nc program. Test if connections can be accepted normally."
echo "#####################################################"
