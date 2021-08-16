#!/bin/sh

tap_driver=$1
shift

current_dir=$(dirname "$0")

args=("$@")
location=${#args[@]}

program=${args[${location} - 1]}
args[${location} - 1]="${current_dir}/driver"
args[${location}]=$program

$tap_driver "${args[@]}"