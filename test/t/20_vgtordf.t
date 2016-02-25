#!/usr/bin/env bash
#
BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH # for vg

plan tests 3

is $(vg construct -r tiny/tiny.fa -v tiny/tiny.vcf.gz | vg view -t -r 'http://example.org' - | wc -l) 90 "vg view produces the expected number of lines of turtle"
is $(vg construct -r tiny/tiny.fa -v tiny/tiny.vcf.gz | vg view -t -r 'http://example.org/' - | wc -l) 90 "vg view produces the expected number of lines of turtle"
is $(vg construct -r tiny/tiny.fa -v tiny/tiny.vcf.gz | vg view -t -r 'http://example.org/' - | rapper -c --input turtle -I "http://example.org/vg" -; echo $?) 0 "rapper passed"
