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

cd test_program1/bin
./test_program $io_flag > result1 &
cd -
sleep 1

cd test_program2/bin
for ((i=0;i<100;++i))
do
    ./test_program $io_flag > result1 &
    sleep 0.01
done
cd -

sleep 3
cd test_program1/bin
RESULT_CNT1=$(grep 'ON_CONNECTED' result1 | wc -l)
echo "RESULT_CNT1 = $RESULT_CNT1"
RESULT_CNT2=$(grep 'ON_CLOSED' result1 | wc -l)
echo "RESULT_CNT2 = $RESULT_CNT2"
cd -

killall -9 test_program
sleep 1

echo "#####################################################"
echo "# Directory  : tcp"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 100 && $RESULT_CNT2 -eq 0 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Create a tcp listener, then create 100 connector to connect this listener. Check if they can all connect to listener normally or not."
echo "#####################################################"
