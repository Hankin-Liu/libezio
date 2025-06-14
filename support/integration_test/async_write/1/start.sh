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
rm -f *.txt
./test_program $io_flag > org.txt &

sleep 8
killall -9 test_program

diff org.txt output.txt
RESULT_CNT1=$?
echo "RESULT_CNT1 = $RESULT_CNT1"
RESULT_CNT2=$(wc -l org.txt | awk '{print $1}')
echo "RESULT_CNT2 = $RESULT_CNT2"
cd -

echo "#####################################################"
echo "# Directory  : async_write"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 0 && $RESULT_CNT2 -eq 10000 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Create 1 file, Write 10000 random strings into this file using submit_async_write interface. At the same time, print these strings into stdout. Finally, compare contents between stdout and the created file,"
echo "#####################################################"
