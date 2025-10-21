## Utility Scripts

This folder contains example jobscripts for an HPC cluster using SLURM as the job manager.  
The scripts are mostly ready to use as-is, specifically for the MareNostrum 5 supercomputer.

The folder contains the following scripts:

1. `jobscript-basic.sh`: A basic jobscript describing nOS-V's default execution settings, where a separate nOS-V instance is launched for each process.
1. `jobscript-shared.sh`: Demonstrates how to use a single nOS-V instance shared across multiple processes.
1. `jobscript-shared-numa.sh`: Shows how to create a separate nOS-V instance per NUMA node, each shared by multiple processes. Since SLURM environment variables are used, an additional helper script is required: `inner-shared-numa.sh`.
1. `jobscript-coexec.sh`: A jobscript describing how to co-execute two applications, each with multiple processes, managed by a single shared nOS-V instance.

Other execution strategies can be derived by combining some of these scripts. For instance, combining the `shared-numa` and `coexec` scripts would result in a co-execution of applications while preventing cross-NUMA resource sharing.

