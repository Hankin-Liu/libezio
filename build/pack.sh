#!/bin/bash

function check_ret()
{
    if [[ $? != 0 ]];then
        echo "[FAILED] $1"
        exit -1
    fi
    echo "[SUC] $1"
}

version="0.0.3.20240706"
if [[ $# -ge 1 ]]; then
    version=$1
fi
package_name="libStableEvent_$version"
rm -rf $package_name
mkdir $package_name
check_ret "mkdir $package_name"

mkdir -p ./$package_name/lib
check_ret "mkdir -p ./$package_name/lib"
mkdir -p ./$package_name/include
check_ret "mkdir -p ./$package_name/include"

cp ../lib/*.a $package_name/lib/
check_ret "cp ../lib/*.a $package_name/lib/"
cp ../lib/*.so $package_name/lib/
check_ret "cp ../lib/*.so $package_name/lib/"
cp -r ../include/* $package_name/include/
check_ret "cp -r ../include/* $package_name/include/"

tar zcvf $package_name.tgz $package_name
check_ret "tar zcvf $package_name.tgz $package_name"
