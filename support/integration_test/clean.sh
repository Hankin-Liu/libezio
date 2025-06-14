#!/usr/bin/env bash

for name in `ls`
do
    if [[ -d $name ]]; then
        sh do_clean.sh -d $name
    fi
done
