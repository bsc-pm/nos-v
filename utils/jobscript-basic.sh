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

# NOTE: This jobscript executes 'myapp.bin' on a system with two sockets, each with 56 cores. It uses nOS-V's default settings:
# - A separate instance per process (as the 'isolation_level' defaults to 'process'), resulting in a total of two nOS-V instances.
# - Each instance manages, and has access to, 56 distinct physical cores.

srun $binary

EOF
}

jobname="basic_app"
binary="myapp.bin"

submit

