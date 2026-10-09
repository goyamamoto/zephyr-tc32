# SPDX-License-Identifier: Apache-2.0

# The devicetree preprocessor needs a target the compiler knows: clang for
# ARMv4T Thumb (arch/tc32/CMakeLists.txt).
list(APPEND DTS_EXTRA_CPPFLAGS --target=thumbv4t-none-eabi)
