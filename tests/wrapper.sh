#!/bin/sh

top_srcdir=$1
shift

test_driver=$1
shift

tap_driver=$1
shift

current_dir=$(dirname "$0")
export NOSV_CONFIG=${top_srcdir}/nosv.toml

args=("$@")
location=${#args[@]}

program=${args[${location} - 1]}
args[${location} - 1]="${test_driver}"
args[${location}]=$program

$tap_driver "${args[@]}"
