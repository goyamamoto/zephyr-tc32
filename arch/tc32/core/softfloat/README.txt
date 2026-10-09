SPDX-License-Identifier: Apache-2.0

Soft-float arithmetic, comparisons and conversions for the TC32, which has no
floating-point unit: the C implementations of compiler-rt's builtins,
unchanged, from llvm-project llvmorg-23.1.2 (85ac560),
compiler-rt/lib/builtins. They define the libgcc names (__addsf3, __ltsf2,
...) and, for the ARMv4T Thumb build, the run-time ABI names (__aeabi_fadd,
...); compiler-rt has the comparison helpers __aeabi_fcmp* and __aeabi_dcmp*
in ARM assembly only, so ../softfloat_aeabi.c gives them in C.
License: Apache-2.0 WITH LLVM-exception (LICENSES/).
