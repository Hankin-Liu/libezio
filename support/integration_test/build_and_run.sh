#!/usr/bin/env bash

function print_usage()
{
    echo "Usage: sh build_and_run.sh [OPTION...]
Build and run test cases which in same level folder automatically.

USAGE: sh build_and_run.sh [-h] [-B] [-b] [-R] [-r] [-i io_flag] [-d directory]

  -B                       Build test programs only
  -b                       Build test programs only
  -R                       Run test cases only
  -r                       Run test cases only
  -i io_flag               IO model. 0 - epoll, 1 - io_uring.
  -d directory             Directory of test cases which need to build and run
  -h                       Give this help list"
}


BUILD_FLAG=0
RUN_FLAG=0
io_flag="0"
folder="./"

while getopts "BbRri:d:h" opt; do
    case "$opt" in
        "B")
            BUILD_FLAG=1
            ;;
        "b")
            BUILD_FLAG=1
            ;;
        "h")
            print_usage
            exit 0
            ;;
        "R")
            RUN_FLAG=1
            ;;
        "r")
            RUN_FLAG=1
            ;;
        "i")
            io_flag=$OPTARG
            ;;
        "d")
            folder=$OPTARG
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

cur_dir=$(pwd)
if [[ ! -d "$folder" ]];then
    echo "Folder:[$folder] does not exist"
    print_usage
    exit -1
fi
cd $folder

# build
if [[ $BUILD_FLAG -eq 1 ]]; then
    for name in `ls`
    do
        if [[ -d $name ]]; then
            cd $name
            for name1 in `ls`
            do
                if [[ -d $name1 ]]; then
                    cd $name1
                    if [[ -e "build" ]]; then
                        cd build
                        rm -rf *
                        cmake ../
                        make -j
                        cd ..
                    fi
                    cd ..
                fi
            done
            cd ..
        fi
    done
fi

# run test
if [[ $RUN_FLAG -eq 1 ]]; then
    for name in `ls`
    do
        if [[ -d $name ]]; then
            echo "Go into directory $name"
            cd $name
            if [[ -e "start.sh" ]]; then
                sh start.sh -i $io_flag
            else
                echo "$name don't have start.sh"
            fi
            echo "Leave directory $name"
            cd ..
        fi
    done
fi

cd $cur_dir
