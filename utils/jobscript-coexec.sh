#!/bin/bash -x

function submit() {
sbatch << EOF
#!/bin/bash -x
#SBATCH --qos=gp_bsccs
#SBATCH --nodes=1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=56
#SBATCH --hint=nomultithread
#SBATCH --exclusive
#SBATCH -A bsc15
#SBATCH -J $jobname
#SBATCH -o $jobname.out%j
#SBATCH -e $jobname.err%j
#SBATCH --time=02:00:00

# NOTE: This jobscript co-executes two appliations ('A.bin' and 'B.bin') on a system with two sockets, each with 56 cores. It overrides nOS-V's default settings:
# - All processes from both applications will use the same name and binding for the nOS-V shared memory, resulting in a single instance.
# - The only nOS-V instance will manage all the cores from the specified topology, for both applications.
# - All processes from both applications will share resources, managed by the quantum of nOS-V's centralized scheduler.
# - 'overcommit' must be used to signal that we are launching processes requesting more physical cores than those available.
# - Although both applications will co-execute, nOS-V will prohibit oversubscription across applications.

export NOSV_CONFIG_OVERRIDE="shared_memory.name=shared,topology.binding=0-111"
srun -N 1 -n 2 -c 56 --overcommit $appA &
srun -N 1 -n 2 -c 56 --overcommit $appB &
wait

EOF
}

jobname="coexec_apps"
appA="A.bin"
appB="B.bin"

submit

