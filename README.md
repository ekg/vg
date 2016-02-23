# vg

[![Join the chat at https://gitter.im/ekg/vg](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/ekg/vg?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

[![Build Status](https://travis-ci.org/ekg/vg.svg)](https://travis-ci.org/ekg/vg)

## variation graph data structures, interchange formats, alignment, genotyping, and variant calling methods

![Variation graph](https://raw.githubusercontent.com/ekg/vg/master/doc/figures/vg_logo.png)

_Variation graphs_ provide a succinct encoding of the sequences of many genomes. A variation graph (in particular as implemented in vg) is composed of:

* _nodes_, which are labeled by sequences and ids
* _edges_, which connect two nodes via either of their respective ends
* _paths_, describe genomes, sequence alignments, and annotations (such as gene models and transcripts) as walks through nodes connected by edges

This model is similar to a number of sequence graphs that have been used in assembly and multiple sequence alignment. Paths provide coordinate systems relative to genomes encoded in the graph, allowing stable mappings to be produced even if the structure of the graph is changed. For visual documentation, please refer to a presentation on the topic: [Resequencing against a human whole genome variation graph](https://docs.google.com/presentation/d/1bbl2zY4qWQ0yYBHhoVuXb79HdgajRotIUa_VEn3kTpI/edit?usp=sharing) (April 14, 2015).

## Usage

### building

Before you begin, you'll need to install some basic tools if they are not already installed. You'll need the protobuf and jansson development libraries installed on your server. Additionally, to run the tests, you will need jq and bc.

    sudo apt-get install build-essential git cmake pkg-config libncurses-dev libbz2-dev  \
                         protobuf-compiler libprotoc-dev libjansson-dev automake libtool \
                         jq bc curl unzip  redland-util librdf-devs

You can also run `make get-deps`.

Other libraries may be required. Please report any build difficulties.

Now, obtain the repo and its submodules:

    git clone --recursive https://github.com/ekg/vg.git

Then build with `. ./source_me.sh && make static`, and run with `./bin/vg`.

#### building on Mac OS X

##### using Mac Ports

VG won't build with XCode's compiler (clang), but it should work with GCC 4.9.  One way to install the latter (and other dependencies) is to install [Mac Ports](https://www.macports.org/install.php), then run:

    sudo port install gcc49 libtool jansson jq cmake pkgconfig autoconf automake libtool coreutils samtools redland-utils

To make GCC 4.9 the default compiler, run (use `none` instead of `mp-gcc49` to revert back):

    sudo port select gcc mp-gcc49

VG can now be cloned and built:

    git clone --recursive https://github.com/ekg/vg.git
    cd vg
    . ./source_me.sh && make
    
Note that static binaries cannot yet be built for Mac.

##### using Homebrew

[Homebrew](http://brew.sh/) provides another package management solution for OSX, and may be preferable to some users over MacPorts.

```
brew tap homebrew/versions  # for gcc49
brew tap homebrew/science  # for samtools
brew install automake libtool jq jansson coreutils gcc49 samtools raptor
export PATH="/usr/local/opt/coreutils/libexec/gnubin:/usr/local/bin:$PATH"

# Set nessary symlinks within /usr/local/bin
(
  cd /usr/local/bin
  # Make symlinks to use glibtool/ize
  ln -s glibtool libtool
  ln -s glibtoolize libtoolize
  # Make symlinks to use gxx-4.9 instead of builtin gxx
  ln -s gcc-4.9 gcc
  ln -s g++-4.9 g++
)

export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH;
export LIBRARY_PATH=$LD_LIBRARY_PATH;

. ./source_me.sh && make
```

### Variation graph construction

The simplest thing to do with `vg` is to build a graph and align to it. At present, you'll want to use a reference and VCF file to do so. If you're working in the `test/` directory:

```sh
vg construct -r small/x.fa -v small/x.vcf.gz >x.vg
```

### Viewing, conversion

`vg view` provides a way to convert the graph into various formats:

```sh
# GFA output
vg view x.vg >x.gfa

# dot output suitable for graphviz
vg view -d x.vg >x.dot

# json version of binary alignments
vg view -a x.gam >x.json
```

### Alignment

As this is a small graph, you could align to it using a full-length partial order alignment:

```sh
vg align -s CTACTGACAGCAGAAGTTTGCTGTGAAGATTAAATTAGGTGATGCTTG x.vg
```

Note that you don't have to store the graph on disk at all, you can simply pipe it into the local aligner:

```sh
vg construct -r small/x.fa -v small/x.vcf.gz | vg align -s CTACTGACAGCAGAAGTTTGCTGTGAAGATTAAATTAGGTGATGCTTG -
```

Most commands allow the streaming of graphs into and out of `vg`.

### Mapping

If your graph is large, you want to use `vg index` to store the graph and `vg map` to align reads. `vg map` implements a kmer based seed and extend alignment model that is similar to that used in aligners like novoalign or MOSAIK. First an on-disk index is built with `vg index` which includes the graph itself and kmers of a particular size. When mapping, any kmer size shorter than that used in the index can be employed, and by default the mapper will decrease the kmer size to increase sensitivity when alignment at a particular _k_ fails.

```sh
# construct the graph
vg construct -r small/x.fa -v small/x.vcf.gz >x.vg

# store the graph in the xg/gcsa index pair
vg index -x x.xg -g x.gcsa -k 11 x.vg

# alternatively, store in a rocksdb backed index
vg index -s -k 11 x.vg

# align a read to the indexed version of the graph
# note that the graph file is not opened, but x.vg.index is assumed
vg map -s CTACTGACAGCAGAAGTTTGCTGTGAAGATTAAATTAGGTGATGCTTG -x x.xg -g x.gcsa -k 22 >read.gam

# simulate a bunch of 150bp reads from the graph and map them
vg map -r <(vg sim -n 1000 -l 150 x.vg) -x x.xg -g x.gcsa -k 22 >aln.gam

# surject the alignments back into the reference space of sequence "x", yielding a BAM file
# NB: currently requires the rocksdb-backed index
vg surject -p x -b aln.gam >aln.bam
```

### Command line interface

A variety of commands are available:

- *construct*: graph construction
- *view*: conversion (dot/protobuf/json/GFA)
- *index*: index features of the graph in a disk-backed key/value store
- *find*: use an index to find nodes, edges, kmers, or positions
- *paths*: traverse paths in the graph
- *align*: local alignment
- *map*: global alignment (kmer-driven)
- *stats*: metrics describing graph properties
- *join*: combine graphs (parallel)
- *concat*: combine graphs (serial)
- *ids*: id manipulation
- *kmers*: generate kmers from a graph
- *sim*: simulate reads by walking paths in the graph
- *mod*: various transformations of the graph
- *surject*: force graph alignments into a linear reference space
- *msga*: construct a graph from an assembly of multiple sequences

## Implementation notes

`vg` is based around a graph object (vg::VG) which has a native serialized representation that is almost identical on disk and in-memory, with the exception of adjacency indexes that are built when the object is parsed from a stream or file. These graph objects are the results of queries of larger indexes, or manipulation (for example joins or concatenations) of other graphs. vg is designed for interactive, stream-oriented use. You can, for instance, construct a graph, merge it with another one, and pipe the result into a local alignment process. The graph object can be stored in an index (vg::Index), aligned against directly (vg::GSSWAligner), or "mapped" against in a global sense (vg::Mapper), using an index of kmers.

Once constructed, a variation graph (.vg is the suggested file extension) is typically around the same size as the reference (FASTA) and uncompressed variant set (VCF) which were used to build it. The rocksdb-based index, however, may be much larger, perhaps more than an order of magnitude. This is less of a concern as it is not loaded into memory, but could be a pain point as vg is scaled up to whole-genome mapping.

The serialization of very large graphs (>62MB) is enabled by the use of protocol buffer ZeroCopyStreams. Graphs are decomposed into sets of N (presently 10k) nodes, and these are written, with their edges, into graph objects that can be streamed into and out of vg. Graphs of unbounded size are possible using this approach.

## License

MIT
