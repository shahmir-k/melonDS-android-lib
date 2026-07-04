/*
    Copyright 2016-2026 melonDS team, RSDuck

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#define ARM_CPSR_offset 0x64
#define ARM_Cycles_offset 0xc
#define ARM_StopExecution_offset 0x10
// liteDS-v2 Unit 2: slice-budget slot. Hand-maintained like the offsets above;
// proven equal to offsetof(ARM, CyclesBudget) by static_assert in the JIT
// compiler TUs (ARMJIT_A64/ARMJIT_Compiler.cpp, ARMJIT_x64/ARMJIT_Compiler.cpp).
#define ARM_CyclesBudget_offset 0xe8
