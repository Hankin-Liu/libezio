#!/bin/bash

function check_ret()
{
    if [[ $? -ne 0 ]];then
        echo "[FAILED] $1"
        exit -1
    else
        echo "[SUCCEED] $1"
    fi
}

function usage()
{
    echo "
Usage: sh build.sh [-j parallel_count]           Parallel compile. make -j \$parallel_count
    "
}

parallel_count=1
while getopts "hj:" opt;
do
    case ${opt} in
        h)  usage
            exit 0
            ;;
        j)  parallel_count=${OPTARG};;
        ?)  echo "unknown parameter"
            usage
            exit -1
            ;;
    esac
done


mkdir -p build
check_ret "make -p build"

cd build
check_ret "cd build"

cmake ../../
check_ret "cmake ../../"

make -j ${parallel_count}
check_ret "make -j ${parallel_count}"

cd -
