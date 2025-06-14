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
pid=$(ps -ef | grep test_program | grep -v grep | awk '{print $2}')

>expect_result
signal="1 2 3 5 8 10 12 13 14 15 17 18 20 21 22 23 24 25 26 27 28 29 30"
for sig in $signal
do
    kill -${sig} $pid
    sleep 0.05
    echo "receive signal ${sig}" >> expect_result
done

killall -9 test_program

diff result1 expect_result
RESULT1=$?
echo "RESULT1 = $RESULT1"

echo "#####################################################"
echo "# Directory  : signal"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT1 -eq 0 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Test signal_event. Test program uses signal_event to block signals, then use kill command to send signals to this program. Check if it can catch the signal correctly or not!"
echo "#####################################################"
