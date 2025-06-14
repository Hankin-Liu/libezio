#!/usr/bin/env bash

function print_usage()
{
    echo "Usage: sh run_all_test.sh [OPTION...]
Run all test cases automatically.

USAGE: sh run_all_test.sh [-h] [-B] [-b] [-R] [-r] [-i io_flag]

  -B                       Build test programs only
  -b                       Build test programs only
  -R                       Run test cases only
  -r                       Run test cases only
  -i io_flag               IO model. 0 - epoll, 1 - io_uring.
  -h                       Give this help list"
}

build_flag=""
run_flag=""
io_flag="0"    # use epoll by default

while getopts "BbRri:h" opt; do
    case "$opt" in
        "B")
            build_flag="-B"
            ;;
        "b")
            build_flag="-B"
            ;;
        "h")
            print_usage
            exit 0
            ;;
        "R")
            run_flag="-R"
            ;;
        "r")
            run_flag="-R"
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

if [[ -z "$build_flag" && -z "$run_flag" ]];then
    build_flag="-B"
    run_flag="-R"
fi

# add permisstion for all scripts
find . -name "*.sh" -print | xargs chmod u+x

for name in `ls`
do
    if [[ -d $name ]]; then
        if [[ -e "build_and_run.sh" ]]; then
            sh build_and_run.sh $build_flag $run_flag -i $io_flag -d $name
        fi
    fi
done
