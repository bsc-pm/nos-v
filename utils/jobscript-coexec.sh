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

# NOTE: This jobscript co-executes two applications ('A.bin' and 'B.bin') on a system with two sockets, each with 56 cores. It overrides nOS-V's default settings:
# - All processes will use the same topology binding for the nOS-V shared memory.
# - The shared memory's name is unchanged ("nosv" by default) and the isolation_level is set to "user", resulting in a single nOS-V instance.
# - The unique nOS-V instance will manage all the cores from the specified topology.
# - All processes from both applications will share resources, managed by the quantum of nOS-V's centralized scheduler.
# - 'overcommit' must be used to signal that we are launching processes requesting more physical cores than those available.
# - Although both applications will co-execute, nOS-V will prohibit oversubscription across applications.

export NOSV_CONFIG_OVERRIDE="shared_memory.isolation_level=user,topology.binding=0-111"
srun -N 1 -n 2 -c 56 --overcommit $appA &
srun -N 1 -n 2 -c 56 --overcommit $appB &
wait

EOF
}

jobname="coexec_apps"
appA="A.bin"
appB="B.bin"

submit

