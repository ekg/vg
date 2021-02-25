// SPDX-FileCopyrightText: 2014 Erik Garrison
//
// SPDX-License-Identifier: MIT

#ifndef VG_GRAPH_CALLER_HPP_INCLUDED
#define VG_GRAPH_CALLER_HPP_INCLUDED

#include <iostream>
#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <tuple>
#include "handle.hpp"
#include "snarls.hpp"
#include "traversal_finder.hpp"
#include "snarl_caller.hpp"
#include "region.hpp"
#include "vg/io/alignment_emitter.hpp"

namespace vg {

using namespace std;

using vg::io::AlignmentEmitter;

/**
 * GraphCaller: Use the snarl decomposition to call snarls in a graph
 */
class GraphCaller {
public:
    GraphCaller(SnarlCaller& snarl_caller,
                SnarlManager& snarl_manager);

    virtual ~GraphCaller();

    /// Run call_snarl() on every top-level snarl in the manager.
    /// For any that return false, try the children, etc. (when recurse_on_fail true)
    /// Snarls are processed in parallel
    virtual void call_top_level_snarls(const HandleGraph& graph,
                                       int ploidy,
                                       bool recurse_on_fail = true);

    /// For every chain, cut it up into pieces using max_edges and max_trivial to cap the size of each piece
    /// then make a fake snarl for each chain piece and call it.  If a fake snarl fails to call,
    /// It's child chains will be recursed on (if selected)_
    virtual void call_top_level_chains(const HandleGraph& graph,
                                       int ploidy,
                                       size_t max_edges,
                                       size_t max_trivial,
                                       bool recurse_on_fail = true);

    /// Call a given snarl, and print the output to out_stream
    virtual bool call_snarl(const Snarl& snarl, int ploidy) = 0;

protected:

    /// Break up a chain into bits that we want to call using size heuristics
    vector<Chain> break_chain(const HandleGraph& graph, const Chain& chain, size_t max_edges, size_t max_trivial);
    
protected:

    /// Our Genotyper
    SnarlCaller& snarl_caller;

    /// Our snarls
    SnarlManager& snarl_manager;
};

/**
 * Helper class that vcf writers can inherit from to for some common code to output sorted VCF
 */
class VCFOutputCaller {
public:
    VCFOutputCaller(const string& sample_name);
    virtual ~VCFOutputCaller();

    /// Write the vcf header (version and contigs and basic info)
    virtual string vcf_header(const PathHandleGraph& graph, const vector<string>& contigs,
                              const vector<size_t>& contig_length_overrides) const;

    /// Add a variant to our buffer
    void add_variant(vcflib::Variant& var) const;

    /// Sort then write variants in the buffer
    void write_variants(ostream& out_stream) const;
    
protected:

    /// print a vcf variant 
    void emit_variant(const PathPositionHandleGraph& graph, SnarlCaller& snarl_caller,
                      const Snarl& snarl, const vector<SnarlTraversal>& called_traversals,
                      const vector<int>& genotype, int ref_trav_idx, const unique_ptr<SnarlCaller::CallInfo>& call_info,
                      const string& ref_path_name, int ref_offset, bool genotype_snarls) const;

    /// get the interval of a snarl from our reference path using the PathPositionHandleGraph interface
    /// the bool is true if the snarl's backward on the path
    tuple<size_t, size_t, bool, step_handle_t, step_handle_t> get_ref_interval(const PathPositionHandleGraph& graph, const Snarl& snarl,
                                                                               const string& ref_path_name) const;

    /// clean up the alleles to not share common prefixes / suffixes
    /// if len_override given, just do that many bases without thinking
    void flatten_common_allele_ends(vcflib::Variant& variant, bool backward, size_t len_override) const;
    
    /// output vcf
    mutable vcflib::VariantCallFile output_vcf;

    /// Sample name
    string sample_name;

    /// output buffers (1/thread) (for sorting)
    mutable vector<vector<vcflib::Variant>> output_variants;

    /// print up to this many uncalled alleles when doing ref-genotpes in -a mode
    size_t max_uncalled_alleles = 5;
};

/**
 * Helper class for outputing snarl traversals as GAF
 */
class GAFOutputCaller {
public:
    /// The emitter object is created and owned by external forces
    GAFOutputCaller(AlignmentEmitter* emitter, const string& sample_name, const vector<string>& ref_paths,
                    size_t trav_padding);
    virtual ~GAFOutputCaller();

    /// print the GAF traversals
    void emit_gaf_traversals(const PathHandleGraph& graph, const vector<SnarlTraversal>& travs);

    /// print the GAF genotype
    void emit_gaf_variant(const HandleGraph& graph,
                          const Snarl& snarl,
                          const vector<SnarlTraversal>& traversals,
                          const vector<int>& genotype);

    /// pad a traversal with (first found) reference path, adding up to trav_padding to each side
    SnarlTraversal pad_traversal(const PathHandleGraph& graph, const SnarlTraversal& trav) const;
    
protected:
    
    AlignmentEmitter* emitter;

    /// Sample name
    string gaf_sample_name;

    /// Add padding from reference paths to traversals to make them at least this long
    /// (only in emit_gaf_traversals(), not emit_gaf_variant)
    size_t trav_padding = 0;

    /// Reference paths are used to pad out traversals.  If there are none, then first path found is used
    unordered_set<string> ref_paths;

};

/**
 * VCFGenotyper : Genotype variants in a given VCF file
 */
class VCFGenotyper : public GraphCaller, public VCFOutputCaller, public GAFOutputCaller {
public:
    VCFGenotyper(const PathHandleGraph& graph,
                 SnarlCaller& snarl_caller,
                 SnarlManager& snarl_manager,
                 vcflib::VariantCallFile& variant_file,
                 const string& sample_name,
                 const vector<string>& ref_paths,
                 FastaReference* ref_fasta,
                 FastaReference* ins_fasta,
                 AlignmentEmitter* aln_emitter,
                 bool traversals_only,
                 bool gaf_output,
                 size_t trav_padding);

    virtual ~VCFGenotyper();

    virtual bool call_snarl(const Snarl& snarl, int ploidy);

    virtual string vcf_header(const PathHandleGraph& graph, const vector<string>& contigs,
                              const vector<size_t>& contig_length_overrides = {}) const;

protected:

    /// get path positions bounding a set of variants
    tuple<string, size_t, size_t>  get_ref_positions(const vector<vcflib::Variant*>& variants) const;

    /// munge out the contig lengths from the VCF header
    virtual unordered_map<string, size_t> scan_contig_lengths() const;

protected:

    /// the graph
    const PathHandleGraph& graph;

    /// input VCF to genotype, must have been loaded etc elsewhere
    vcflib::VariantCallFile& input_vcf;

    /// traversal finder uses alt paths to map VCF alleles from input_vcf
    /// back to traversals in the snarl
    VCFTraversalFinder traversal_finder;

    /// toggle whether to genotype or just output the traversals
    bool traversals_only;

    /// toggle whether to output vcf or gaf
    bool gaf_output;
};


/**
 * LegacyCaller : Preserves (most of) the old vg call logic by using 
 * the RepresentativeTraversalFinder to recursively find traversals
 * through arbitrary sites.   
 */
class LegacyCaller : public GraphCaller, public VCFOutputCaller {
public:
    LegacyCaller(const PathPositionHandleGraph& graph,
                 SupportBasedSnarlCaller& snarl_caller,
                 SnarlManager& snarl_manager,
                 const string& sample_name,
                 const vector<string>& ref_paths = {},
                 const vector<size_t>& ref_path_offsets = {});

    virtual ~LegacyCaller();

    virtual bool call_snarl(const Snarl& snarl, int ploidy);

    virtual string vcf_header(const PathHandleGraph& graph, const vector<string>& contigs,
                              const vector<size_t>& contig_length_overrides = {}) const;

protected:

    /// recursively genotype a snarl
    /// todo: can this be pushed to a more generic class? 
    pair<vector<SnarlTraversal>, vector<int>> top_down_genotype(const Snarl& snarl, TraversalFinder& trav_finder, int ploidy,
                                                                const string& ref_path_name, pair<size_t, size_t> ref_interval) const;
    
    /// we need the reference traversal for VCF, but if the ref is not called, the above method won't find it. 
    SnarlTraversal get_reference_traversal(const Snarl& snarl, TraversalFinder& trav_finder) const;

    /// re-genotype output of top_down_genotype.  it may give slightly different results as
    /// it's working with fully-defined traversals and can exactly determine lengths and supports
    /// it will also make sure the reference traversal is in the beginning of the output
    tuple<vector<SnarlTraversal>, vector<int>, unique_ptr<SnarlCaller::CallInfo>> re_genotype(const Snarl& snarl,
                                                                                              TraversalFinder& trav_finder,
                                                                                              const vector<SnarlTraversal>& in_traversals,
                                                                                              const vector<int>& in_genotype,
                                                                                              int ploidy,
                                                                                              const string& ref_path_name,
                                                                                              pair<size_t, size_t> ref_interval) const;

    /// check if a site can be handled by the RepresentativeTraversalFinder
    bool is_traversable(const Snarl& snarl);

    /// look up a path index for a site and return its name too
    pair<string, PathIndex*> find_index(const Snarl& snarl, const vector<PathIndex*> path_indexes) const;

protected:

    /// the graph
    const PathPositionHandleGraph& graph;
    /// non-vg inputs are converted into vg as-needed, at least until we get the
    /// traversal finding ported
    bool is_vg;

    /// The old vg call traversal finder.  It is fairly efficient but daunting to maintain.
    /// We keep it around until a better replacement is implemented.  It is *not* compatible
    /// with the Handle Graph API because it relise on PathIndex.  We convert to VG as
    /// needed in order to use it. 
    RepresentativeTraversalFinder* traversal_finder;
    /// Needed by above (only used when working on vg inputs -- generated on the fly otherwise)
    vector<PathIndex*> path_indexes;

    /// keep track of the reference paths
    vector<string> ref_paths;

    /// keep track of offsets in the reference paths
    map<string, size_t> ref_offsets;

    /// Tuning

    /// How many nodes should we be willing to look at on our path back to the
    /// primary path? Keep in mind we need to look at all valid paths (and all
    /// combinations thereof) until we find a valid pair.
    int max_search_depth = 1000;
    /// How many search states should we allow on the DFS stack when searching
    /// for traversals?
    int max_search_width = 1000;
    /// What's the maximum number of bubble path combinations we can explore
    /// while finding one with maximum support?
    size_t max_bubble_paths = 100;

};


/**
 * FlowCaller : Uses any traversals finder (ex, FlowTraversalFinder) to find 
 * traversals, and calls those based on how much support they have.  
 * Should work on any graph but will not
 * report cyclic traversals.  Does not (yet, anyway) support nested
 * calling, so the entire site is processes in one shot. 
 * Designed to replace LegacyCaller, as it should miss fewer obviously
 * good traversals, and is not dependent on old protobuf-based structures. 
 */
class FlowCaller : public GraphCaller, public VCFOutputCaller, public GAFOutputCaller {
public:
    FlowCaller(const PathPositionHandleGraph& graph,
               SupportBasedSnarlCaller& snarl_caller,
               SnarlManager& snarl_manager,
               const string& sample_name,
               TraversalFinder& traversal_finder,
               const vector<string>& ref_paths,
               const vector<size_t>& ref_path_offsets,
               AlignmentEmitter* aln_emitter,
               bool traversals_only,
               bool gaf_output,
               size_t trav_padding,
               bool genotype_snarls);
   
    virtual ~FlowCaller();

    virtual bool call_snarl(const Snarl& snarl, int ploidy);

    virtual string vcf_header(const PathHandleGraph& graph, const vector<string>& contigs,
                              const vector<size_t>& contig_length_overrides = {}) const;

protected:

    /// the graph
    const PathPositionHandleGraph& graph;

    /// the traversal finder
    TraversalFinder& traversal_finder;

    /// keep track of the reference paths
    vector<string> ref_paths;
    unordered_set<string> ref_path_set;

    /// keep track of offsets in the reference paths
    map<string, size_t> ref_offsets;

    /// until we support nested snarls, cap snarl size we attempt to process
    size_t max_snarl_edges = 500000;

    /// alignment emitter. if not null, traversals will be output here and
    /// no genotyping will be done
    AlignmentEmitter* alignment_emitter;

    /// toggle whether to genotype or just output the traversals
    bool traversals_only;

    /// toggle whether to output vcf or gaf
    bool gaf_output;

    /// toggle whether to genotype every snarl
    /// (by default, uncalled snarls are skipped, and coordinates are flattened
    ///  out to minimize variant size -- this turns all that off)
    bool genotype_snarls;
};



}

#endif
