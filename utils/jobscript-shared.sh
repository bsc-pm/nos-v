#!/bin/bash -x

function submit() {
sbatch << EOF
#!/bin/bash -x
#SBATCH --qos=gp_bsccs
#SBATCH --nodes=1
#SBATCH --ntasks=2
#SBATCH --cpus-per-task=56
#SBATCH --hint=nomultithread
#SBATCH --exclusive
#SBATCH -A bsc15
#SBATCH -J $jobname
#SBATCH -o $jobname.out%j
#SBATCH -e $jobname.err%j
#SBATCH --time=02:00:00

# NOTE: This jobscript executes 'myapp.bin' on a system with two sockets, each with 56 cores. It overrides nOS-V's default settings:
# - All processes will use the same topology binding for the nOS-V shared memory.
# - The shared memory's name is unchanged ("nosv" by default) and the isolation_level is set to "user", resulting in a single nOS-V instance.
# - The unique nOS-V instance will manage all the cores from the specified topology.
# - All processes will share resources, managed by the quantum of nOS-V's centralized scheduler.
# - Effectively, this is equivalent to using the 'shared-mpi' nOS-V preset instead of the 'NOSV_CONFIG_OVERRIDE' variable:
#   (export NOSV_PRESET="shared-mpi")

export NOSV_CONFIG_OVERRIDE="shared_memory.isolation_level=user,topology.binding=0-111"
srun $binary

EOF
}

jobname="shared_app"
binary="myapp.bin"

submit

