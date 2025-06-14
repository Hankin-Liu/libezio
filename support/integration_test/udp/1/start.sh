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

cd test_program2/bin
./test_program $io_flag > result1 &
cd -
sleep 1

cd test_program1/bin
./test_program $io_flag > result1 &
cd -

sleep 15
grep 'data = \[' test_program1/bin/result1 | awk -F 'data = \\[' '{print $2}' | awk -F '\\]' '{print $1}' > client_send
grep 'data = \[' test_program2/bin/result1 | awk -F 'data = \\[' '{print $2}' | awk -F '\\]' '{print $1}' > server_recv
diff client_send server_recv >diff.out
RESULT_CNT1=$?
echo "RESULT_CNT1 = $RESULT_CNT1"
RESULT_CNT2=$(cat client_send | wc -l)
echo "RESULT_CNT2 = $RESULT_CNT2"

killall -9 test_program

echo "#####################################################"
echo "# Directory  : udp"
echo "# CASE       : 1"
echo "# IO MODEL   : $IO_MODEL"
if [[ $RESULT_CNT1 -eq 0 && $RESULT_CNT2 -eq 10000 ]]; then
    echo "# RESULT     : PASS"
else
    echo "# RESULT     : FAILED"
fi
echo "# DESCRIPTION: Create a udp server and a udp client. Client sends 10000 random string to server. Check if server can receive correctly!"
echo "#####################################################"
