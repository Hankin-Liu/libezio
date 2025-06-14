#!/usr/bin/env bash

function print_usage()
{
    echo "Usage: sh do_clean.sh [OPTION...]
CLean tmp file or useless file automatically.

USAGE: sh do_clean.sh [-h] [-d directory]

  -d directory             Directory of test cases which need to clean
  -h                       Give this help list"
}

folder=""
while getopts "d:h" opt; do
    case "$opt" in
        "h")
            print_usage
            exit 0
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

if [[ ! -d "$folder" ]];then
    echo "Unknown folder : [$folder]"
    exit -1
fi

cur_dir=$(pwd)
cd $folder

for name in `ls`
do
    if [[ -d $name ]]; then
        cd $name
        for name1 in `ls`
        do
            if [[ -d $name1 ]]; then
                cd $name1
                for name2 in `ls`
                do
                    if [[ "$name2" == "bin" || "$name2" == "build" ]]; then
                        rm -rf $name2/*
                    elif [[ "$name2" == "log" ]]; then
                        rm -rf $name2
                    elif [[ "$name2" == "conf" || "$name2" == "src" || "$name2" == "CMakeLists.txt" ]]; then
                        :
                    else
                        echo "unknown file $name2"
                    fi
                done
                cd ..
            else
                if [[ "$name1" == "start.sh" || "$name1" == "README" ]]; then
                    :
                else
                    rm -f $name1
                fi
            fi
        done
        cd ..
    fi
done
cd $cur_dir
