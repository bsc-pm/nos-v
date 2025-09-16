#!/bin/bash -x

binary=$1

# NOTE: This script is an 'inner' script invoked by a jobscript ('jobscript-shared-numa'), launched on a system with two sockets, each with 56 cores.
# The jobscript launches this script on 8 tasks, each with 14 cores, and this inner script leverages the SLURM-defined variable 'SLURM_LOCALID' to 
# override nOS-V's default settings depending on the task's id:
# - Each NUMA node's CPUs (0-55, and 56-111) will be managed by a distinct nOS-V instance.
# - This will result in a nOS-V instance managing CPUs 0 to 55 (nosv-numa0) and another managing 56 to 111 (nosv-numa1).
# - The processes will leverage resource sharing only from within their NUMA nodes, depending on their local ID (see SLURM_LOCALID).
#   (i.e., task 0 will share CPUs 0-55 with tasks 1, 2, and 3, and task 4 will share CPUs 56-111 with tasks 5, 6, and 7).

rank=$SLURM_LOCALID
if [ "$rank" -lt 4 ]; then
    NOSV_CONFIG_OVERRIDE="shared_memory.name=nosv-numa0,topology.binding=0-55" $binary
else
    NOSV_CONFIG_OVERRIDE="shared_memory.name=nosv-numa1,topology.binding=56-111" $binary
fi

