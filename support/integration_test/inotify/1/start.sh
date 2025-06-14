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
>output.txt
stdbuf -oL ./test_program $io_flag > result1 &

for ((i=1; i<=15000; ++i))
do
    echo "$i" >> output.txt
    sleep 0.001
done

sleep 5
killall -2 test_program
RESULT_CNT1=$(cat result1 | wc -l)
echo "RESULT_CNT1 = $RESULT_CNT1"
diff output.txt result1
RESULT_CNT2=$?
echo "RESULT_CNT2 = $RESULT_CNT2"
cd -

echo "#####################################################"
echo "# Directory  : inotify"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 15000 && $RESULT_CNT2 -eq 0 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: test_program reads file output.txt using submit_async_read interface. When it has read some contents, print them to stdout. At the beginning, file output.txt is empty. Then start a process to echo a number from 1 to 15000 into output.txt. When submit_async_read returns 0 which means file is empty, create a file monitor by calling create_watch_obj and watch_file interface. When is notified file is not empty, then calls submit_async_read to read file. Finally, check if test_program has read all the number or not."
echo "#####################################################"
