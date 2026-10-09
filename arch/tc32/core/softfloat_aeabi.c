/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The run-time ABI's floating-point comparison helpers (RTABI 4.1.2), which
 * compiler-rt implements in ARM assembly only: in C over its __eqsf2 family
 * (softfloat/). Those return 1 for unordered operands from the eq/lt/le
 * functions and -1 from the ge/gt functions, so every comparison below is
 * false when an operand is a NaN, as the ABI requires.
 */

int __eqsf2(float a, float b);
int __ltsf2(float a, float b);
int __lesf2(float a, float b);
int __gesf2(float a, float b);
int __gtsf2(float a, float b);
int __eqdf2(double a, double b);
int __ltdf2(double a, double b);
int __ledf2(double a, double b);
int __gedf2(double a, double b);
int __gtdf2(double a, double b);

int __aeabi_fcmpeq(float a, float b)
{
	return __eqsf2(a, b) == 0;
}

int __aeabi_fcmplt(float a, float b)
{
	return __ltsf2(a, b) < 0;
}

int __aeabi_fcmple(float a, float b)
{
	return __lesf2(a, b) <= 0;
}

int __aeabi_fcmpge(float a, float b)
{
	return __gesf2(a, b) >= 0;
}

int __aeabi_fcmpgt(float a, float b)
{
	return __gtsf2(a, b) > 0;
}

int __aeabi_dcmpeq(double a, double b)
{
	return __eqdf2(a, b) == 0;
}

int __aeabi_dcmplt(double a, double b)
{
	return __ltdf2(a, b) < 0;
}

int __aeabi_dcmple(double a, double b)
{
	return __ledf2(a, b) <= 0;
}

int __aeabi_dcmpge(double a, double b)
{
	return __gedf2(a, b) >= 0;
}

int __aeabi_dcmpgt(double a, double b)
{
	return __gtdf2(a, b) > 0;
}
