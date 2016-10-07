#!/usr/bin/env bash

BASH_TAP_ROOT=../deps/bash-tap
. ../deps/bash-tap/bash-tap-bootstrap

PATH=../bin:$PATH # for vg

plan tests 15

vg construct -r 1mb1kgp/z.fa -v 1mb1kgp/z.vcf.gz >z.vg
#is $? 0 "construction of a 1 megabase graph from the 1000 Genomes succeeds"

nodes=$(vg stats -z z.vg | head -1 | cut -f 2)
is $nodes 84557 "vg stats reports the expected number of nodes"

edges=$(vg stats -z z.vg | tail -1 | cut -f 2)
is $edges 115361 "vg stats reports the expected number of edges"

graph_length=$(vg stats -l z.vg | tail -1 | cut -f 2)
is $graph_length 1029257 "vg stats reports the expected graph length"

subgraph_count=$(vg stats -s z.vg | wc -l)
is $subgraph_count 1 "vg stats reports the correct number of subgraphs"

subgraph_length=$(vg stats -s z.vg | head -1 | cut -f 2)
is $subgraph_length $graph_length  "vg stats reports the correct subgraph length"

is $(vg view -Fv msgas/q_redundant.gfa | vg stats -S - | md5sum | cut -f 1 -d\ ) 01fadb6a004ddb87e5fc5d056b565218 "perfect to and from siblings are determined"

vg construct -r tiny/tiny.fa -v tiny/tiny.vcf.gz >t.vg
is $(vg stats -n 13 -d t.vg | cut -f 2) 38 "distance to head is correct"
is $(vg stats -n 13 -t t.vg | cut -f 2) 11 "distance to tail is correct"

is $(vg stats -b t.vg | head -1 | cut -f 3) 1,2,3,4,5,6, "a superbubble's internal nodes are correctly reported"
is $(vg stats -u t.vg | head -1 | cut -f 3) 1,2,3,4,5,6, "a ultrabubble's internal nodes are correctly reported"
rm -f t.vg

is $(cat graphs/missed_bubble.gfa | vg view -Fv - | vg stats -b - | grep 79,80,81,299, | wc -l) 1 "superbubbles are detected even when the graph initially has reversing edges"
is $(cat graphs/missed_bubble.gfa | vg view -Fv - | vg stats -u - | grep 79,80,81,299, | wc -l) 1 "ultrabubbles are detected even when the graph initially has reversing edges"

vg stats z.vg -b > sb.txt
vg stats z.vg -u > cb.txt
is $(diff sb.txt cb.txt | wc -l) 0 "superbubbles and cactus bubbles identical for 1mb1kgp"
rm -f z.vg sb.txt cb.txt

vg construct -r tiny/tiny.fa -v tiny/tiny.vcf.gz | vg mod -X 1 - > tiny.vg
vg stats tiny.vg -b > sb.txt
vg stats tiny.vg -u > cb.txt
is $(diff sb.txt cb.txt | wc -l) 0 "superbubbles and cactus bubbles identical for atomized tiny"

rm sb.txt cb.txt tiny.vg

vg construct -r small/x.fa -a -f -v small/x.vcf.gz >x.vg
vg index -x x.xg x.vg
vg sim -s 1337 -n 100 -x x.xg >x.reads
vg map -V x.vg -k 16 -r x.reads -L 10 >x.gam
is "$(vg stats -a x.gam x.vg | md5sum | cut -f 1 -d\ )" "$(md5sum correct/10_vg_stats/15.txt | cut -f 1 -d\ )" "aligned read stats are computed correctly"
rm -f x.vg x.xg x.gam x.reads

