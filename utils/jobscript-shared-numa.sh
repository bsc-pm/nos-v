#!/bin/bash -x

function submit() {
sbatch << EOF
#!/bin/bash -x
#SBATCH --qos=gp_bsccs
#SBATCH --nodes=1
#SBATCH --ntasks=8
#SBATCH --cpus-per-task=14
#SBATCH --hint=nomultithread
#SBATCH --exclusive
#SBATCH -A bsc15
#SBATCH -J $jobname
#SBATCH -o $jobname.out%j
#SBATCH -e $jobname.err%j
#SBATCH --time=02:00:00

# NOTE: This jobscript executes another script ('inner-shared-numa.sh') on a system with two sockets, each with 56 cores.
# It launches 8 tasks, each with 14 cores, and the inner script overrides nOS-V's default settings depending on each task's id:
# - Each NUMA node's CPUs (0-55, and 56-111) will be managed by a distinct nOS-V instance.
# - This will result in a nOS-V instance managing CPUs 0 to 55 (nosv-numa0) and another managing 56 to 111 (nosv-numa1).
# - The processes will leverage resource sharing only from within their NUMA nodes, depending on their local ID (see SLURM_LOCALID).
#   (e.g., task 2 will share CPUs 0-55 with tasks 0, 1, and 3).

srun inner-shared-numa.sh $binary

EOF
}

jobname="shared_numa_app"
binary="myapp.bin"

submit

