#!/bin/sh
$CC data2code.c -o data2code && ./data2code data.txt >powah_gen_base.hpp
$CC --target=powerpc64le test.S -c -o test && objdump -SC test
