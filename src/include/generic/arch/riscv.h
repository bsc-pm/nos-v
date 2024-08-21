/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_RISCV_H
#define ARCH_RISCV_H

#define ARCH_RISCV
#define ARCH_SUPPORTED

#if defined(ARCH_X86) || defined(ARCH_POWER9) || defined(ARCH_ARM64)
#error "Multiple architecture definitions"
#endif

#define arch_spin_wait_body() __asm__ __volatile__(".insn 0x0100000f") // pause without needing Zhintpause extension
#define arch_spin_wait_release()


#endif // ARCH_RISCV_H
