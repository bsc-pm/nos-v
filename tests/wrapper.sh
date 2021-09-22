#!/bin/sh

test_driver=$1
shift

tap_driver=$1
shift

current_dir=$(dirname "$0")

args=("$@")
location=${#args[@]}

program=${args[${location} - 1]}
args[${location} - 1]="${test_driver}"
args[${location}]=$program

$tap_driver "${args[@]}"