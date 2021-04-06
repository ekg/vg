#pragma once // TODO: remove this, to avoid warnings + maybe bad coding practice?

#include "0_oo_normalize_snarls.hpp"
#include "0_snarl_sequence_finder.hpp"
// #include <algorithm>
#include <string>

#include <deps/seqan/include/seqan/align.h>
#include <deps/seqan/include/seqan/graph_align.h>
#include <deps/seqan/include/seqan/graph_msa.h>
// #include <seqan/align.h>
// #include <seqan/graph_align.h>
// #include <seqan/graph_msa.h>

#include <gbwtgraph/gbwtgraph.h>

#include "../gbwt_helper.hpp"
#include "../handle.hpp"
#include "../msa_converter.hpp"
#include "../snarls.hpp"
#include "../vg.hpp"

#include "../types.hpp"
#include "extract_containing_graph.hpp"

/*
TODO: allow for snarls that have haplotypes that begin or end in the middle of the snarl

TODO: allow normalization of multiple adjacent snarls in one combined realignment.

TODO: test that extract_gbwt haplotypes successfully extracts any haplotypes that start/end in the middle of
TODO:    the snarl.
*/
namespace vg {
namespace algorithms{
/**
 * To "normalize" a snarl, SnarlNormalizer extracts all the sequences in the snarl as
 * represented in the gbwt, and then realigns them to create a replacement snarl. 
 * This process hopefully results in a snarl with less redundant sequence, and with 
 * duplicate variation combined into a single variant.
*/
SnarlNormalizer::SnarlNormalizer(MutablePathDeletableHandleGraph &graph,
                                 const gbwtgraph::GBWTGraph &haploGraph,
                                 const int &max_alignment_size, const string &path_finder)
    : _haploGraph(haploGraph), _graph(graph), _max_alignment_size(max_alignment_size),
      _path_finder(path_finder) {}


/**
 * Iterates over all top-level snarls in _graph, and normalizes them.
 * @param snarl_stream file stream from .snarl.pb output of vg snarls
*/
void SnarlNormalizer::normalize_top_level_snarls(ifstream &snarl_stream) {
    // cerr << "disambiguate_top_level_snarls" << endl;
    SnarlManager *snarl_manager = new SnarlManager(snarl_stream);

    int num_snarls_normalized = 0;
    int num_snarls_skipped = 0;
    vector<const Snarl *> snarl_roots = snarl_manager->top_level_snarls();
    
    /**
     * We keep an error record to observe when snarls are skipped because they aren't 
     * normalizable under current restraints. Bools:
     *      0) snarl exceeds max number of threads that can be efficiently aligned,
     *      1) snarl has haplotypes starting/ending in the middle,
     *      2)  some handles in the snarl aren't connected by a thread,
     *      3) snarl is cyclic.
     * There are two additional ints for tracking the snarl size. Ints:
     *      4) number of bases in the snarl before normalization
     *      5) number of bases in the snarl after normalization.
    */ 
    int error_record_size = 6;
    vector<int> one_snarl_error_record(error_record_size, 0);
    vector<int> full_error_record(error_record_size, 0);

    pair<int, int> snarl_sequence_change;

    // // //todo: debug_code
    // int stop_size = 1;
    // int num_snarls_touched = 0;

    // int skip_first_few = 2; //#1, node 3702578 is a cyclic snarl. Don't recall about #0. #2 also cyclic. Looks like cyclic snarls weren't buggy?
    // int skipped = 0;
    // int snarl_num = 0;
    for (auto roots : snarl_roots) {
        // cerr << "normalizing snarl number " << snarl_num << endl;
        // snarl_num++;
        // if (skipped < skip_first_few){
        //     skipped++;
        //     continue;
        // }
        
        // if (num_snarls_touched == stop_size){
        //     break;
        // } else {
        //     num_snarls_touched++;
        // }
        
        // if (roots->start().node_id() == 3881494) {
            // cerr << "root backwards?" << roots->start().backward() << endl;
            // cerr << "disambiguating snarl #"
            //         << (num_snarls_normalized + num_snarls_skipped)
            //         << " source: " << roots->start().node_id()
            //         << " sink: " << roots->end().node_id() << endl;

            one_snarl_error_record = normalize_snarl(roots->start().node_id(), roots->end().node_id(), roots->start().backward());
            if (!((one_snarl_error_record[0]) || (one_snarl_error_record[1]) ||
                    (one_snarl_error_record[2]) || (one_snarl_error_record[3]))) {
                // if there are no errors, then we've successfully normalized a snarl.
                num_snarls_normalized += 1;
                // track the change in size of the snarl.
                snarl_sequence_change.first += one_snarl_error_record[4];
                snarl_sequence_change.second += one_snarl_error_record[5];
                // cerr << "normalized snarl starting at: " << roots->start().node_id() << endl;
            } else {
                // else, there was an error. Track which errors caused the snarl to not
                // normalize.
                // note: the last two ints are ignored here b/c they're for
                // recording the changing size of snarls that are successfully normalized.
                for (int i = 0; i < error_record_size - 2; i++) {
                    full_error_record[i] += one_snarl_error_record[i];
                }
                num_snarls_skipped += 1;
            }
            
            // //todo: debug_statement for extracting snarl of interest.
            // VG outGraph;
            // pos_t source_pos = make_pos_t(roots->start().node_id(), false, 0);
            // vector<pos_t> pos_vec;
            // pos_vec.push_back(source_pos);
            // algorithms::extract_containing_graph(&_graph, &outGraph, pos_vec, roots->end().node_id() - roots->start().node_id() + 2);
            // outGraph.serialize_to_ostream(cout);
            // break;
        // }

    }
    cerr << endl
         << "normalized " << num_snarls_normalized << " snarl(s), skipped "
         << num_snarls_skipped << " snarls because. . .\nthey exceeded the size limit ("
         << full_error_record[0]
         << "snarls),\nhad haplotypes starting/ending in the middle of the snarl ("
         << full_error_record[1] << "),\nthe snarl was cyclic (" << full_error_record[3]
         << " snarls),\nor there "
            "were handles not connected by the gbwt info ("
         << full_error_record[2] << " snarls)." << endl;
    cerr << "amount of sequence in normalized snarls before normalization: "
         << snarl_sequence_change.first << endl;
    cerr << "amount of sequence in normalized snarls after normalization: "
         << snarl_sequence_change.second << endl;

    //todo: debug_statement for extracting snarl of interest.
    // VG outGraph;
    // pos_t source_pos = make_pos_t(269695, false, 0);
    // vector<pos_t> pos_vec;
    // pos_vec.push_back(source_pos);
    // algorithms::extract_containing_graph(&_graph, &outGraph, pos_vec, 1000);
    // _graph = outGraph;
    // vg::io::VPKG::save(*dynamic_cast<bdsg::HashGraph *>(outGraph.get()), cout);
    // outGraph.serialize_to_ostream(cout);

    delete snarl_manager;
}

/**
 * Normalize a single snarl defined by a source and sink. Only extracts and realigns 
 * sequences found in the gbwt. 
 * @param source_id the source of the snarl of interest.
 * @param sink_id the sink of the snarl of interest.
 * @param error_record an empty vector of 6 integers.
*/
// Returns: none.
// TODO: allow for snarls that have haplotypes that begin or end in the middle of the
// snarl.
vector<int> SnarlNormalizer::normalize_snarl(id_t source_id, id_t sink_id, const bool backwards) {
    // if (backwards){
    //     // swap the source and sink ids. Essentially, this guarantees I treat the leftmost node in snarl as "source".
    //     // (although some adjustments for paths need be made)
    //     id_t swap_source = sink_id; //temp storage of sink_id value. 
    //     sink_id = source_id;
    //     source_id = swap_source; 
    // }

    
    /**
     * We keep an error record to observe when snarls are skipped because they aren't 
     * normalizable under current restraints. Bools:
     *      0) snarl exceeds max number of threads that can be efficiently aligned,
     *      1) snarl has haplotypes starting/ending in the middle,
     *      2)  some handles in the snarl aren't connected by a thread,
     *      3) snarl is cyclic.
     * There are two additional ints for tracking the snarl size. Ints:
     *      4) number of bases in the snarl before normalization
     *      5) number of bases in the snarl after normalization.
    */ 
    vector<int> error_record(6, 0);
    // //todo: debug_statement: determining whether cyclic problem in yeast graph goes away when I swapo source and sink. 
    // SubHandleGraph snarl = extract_subgraph(_graph, sink_id, source_id);
    SubHandleGraph snarl = extract_subgraph(_graph, source_id, sink_id, backwards);

    // //todo: debug_statement: Evaluate connections of all nodes in subgraph.
    // snarl.for_each_handle([&](const handle_t handle){
    //     cerr << "examining left neighbors of handle " << snarl.get_id(handle) << ":" << endl;
    //     snarl.follow_edges(handle, false, [&](const handle_t &next) {
    //         cerr << "     " << snarl.get_id(next) << endl;
    //     });
    // });

    if (!handlealgs::is_acyclic(&snarl)) {
        cerr << "snarl at " << source_id << " is cyclic. Skipping." << endl;
        error_record[3] = true;
        return error_record;
    }

    // extract threads
    // haplotypes is of format:
    // 0: a set of all the haplotypes which stretch from source to sink, in string format.
    //   - it's a set, so doesn't contain duplicates
    // 1: a vector of all the other haps in the snarl (in vector<handle_t> format)
    // 2: a vector of all the handles ever touched by the SnarlSequenceFinder.
    tuple<unordered_set<string>, vector<vector<handle_t>>, unordered_set<handle_t>> haplotypes;
    SnarlSequenceFinder sequence_finder = SnarlSequenceFinder(_graph, snarl, _haploGraph, source_id, sink_id, backwards);
    
    if (_path_finder == "GBWT") {
        tuple<vector<vector<handle_t>>, vector<vector<handle_t>>, unordered_set<handle_t>>
            gbwt_haplotypes = sequence_finder.find_gbwt_haps();
        // Convert the haplotypes from vector<handle_t> format to string format.
        get<0>(haplotypes) = format_handle_haplotypes_to_strings(get<0>(gbwt_haplotypes));
        get<1>(haplotypes) = get<1>(gbwt_haplotypes);
        get<2>(haplotypes) = get<2>(gbwt_haplotypes);
    } else if (_path_finder == "exhaustive") {
        pair<unordered_set<string>, unordered_set<handle_t>> exhaustive_haplotypes =
            sequence_finder.find_exhaustive_paths();
        get<0>(haplotypes) = exhaustive_haplotypes.first;
        get<2>(haplotypes) = exhaustive_haplotypes.second;
    } else {
        cerr << "path_finder type must be 'GBWT' or 'exhaustive', not '" << _path_finder
             << "'." << endl;
        exit(1);
    }

    // check to make sure that the gbwt _graph has threads connecting all handles:
    // ( needs the unordered_set from extract_gbwt haplotypes to be equal to the number of
    // handles in the snarl).
    unordered_set<handle_t> handles_in_snarl;
    snarl.for_each_handle([&](const handle_t handle) {
        handles_in_snarl.emplace(handle);
        // count the number of bases in the snarl.
        error_record[4] += snarl.get_sequence(handle).size();
    });

    // TODO: this if statement only permits snarls that satsify requirements, i.e.
    // TODO:    there are no haplotype begins/ends in the middle
    // TODO:    of the snarl. Get rid of this once alignment issue is addressed!
    // TODO: also, limits the number of haplotypes to be aligned, since snarl starting at
    // TODO:    2049699 with 258 haplotypes is taking many minutes.
    if (get<1>(haplotypes).empty() && get<0>(haplotypes).size() < _max_alignment_size &&
        get<2>(haplotypes).size() == handles_in_snarl.size()) {
        // Get the embedded paths in the snarl from _graph, to move them to new_snarl.
        // Any embedded paths not in gbwt are aligned in the new snarl.
        vector<pair<step_handle_t, step_handle_t>> embedded_paths =
            sequence_finder.find_embedded_paths();

        //todo: debug_statement
        // cerr << "strings in path_seq before adding haplotypes: " << endl;
        // for (auto path : get<0>(haplotypes))
        // {
        //     cerr << path << endl;
        // }

        // TODO: once haplotypes that begin/end in the middle of the snarl have been
        // TODO:    accounted for in the code, remove next chunk of code that finds 
        // TODO: source-to-sink paths.
        // find the paths that stretch from source to sink:
        // cerr << "~~~~~~~~~~source: " << source_id << "sink: " << sink_id << endl;
        for (auto path : embedded_paths) 
        {

            // cerr << "checking path of name " << _graph.get_path_name(_graph.get_path_handle_of_step(path.first)) << " with source " << _graph.get_id(_graph.get_handle_of_step(path.first)) << " and sink " << _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) << endl;
            // cerr << "SOURCE info: prev step: " << _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) << "prev prev step: " << _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(_graph.get_previous_step(path.second)))) << " source: " << _graph.get_id(_graph.get_handle_of_step(path.second)) << " next step: " << _graph.get_id(_graph.get_handle_of_step(_graph.get_next_step(path.second))) << endl;
            // cerr << _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) << " " << source_id << " source bool: " <<  (_graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) == source_id) << endl;
            if (_graph.get_id(_graph.get_handle_of_step(path.first)) == source_id &&
                _graph.get_id(_graph.get_handle_of_step(
                    _graph.get_previous_step(path.second))) == sink_id)  {
                // cerr << "path_seq added to haplotypes. " << _graph.get_path_name(_graph.get_path_handle_of_step(path.first)) << endl;

                // cerr << "******************************************\nadding path of name " <<
                // _graph.get_path_name(_graph.get_path_handle_of_step(path.first)) <<
                // endl; 
                // get the sequence of the source to sink path, and add it to the
                // paths to be aligned.
                string path_seq;
                step_handle_t cur_step = path.first;
                while (cur_step != path.second) {
                    // cerr << "while adding path, looking at node " << _graph.get_id(_graph.get_handle_of_step(cur_step)) << " with seq " << _graph.get_sequence(_graph.get_handle_of_step(cur_step)) << endl;
                    path_seq += _graph.get_sequence(_graph.get_handle_of_step(cur_step));
                    cur_step = _graph.get_next_step(cur_step);
                }
                // cerr << "path seq:" << path_seq << endl;
                if (backwards) {
                    // cerr << "path seq emplaced (in reverse):" << reverse_complement(path_seq)  << endl;
                    // int init_hap_size = get<0>(haplotypes).size(); // Note: just for debug purposes.
                    get<0>(haplotypes).emplace(reverse_complement(path_seq));
                    // cerr << "was path_seq a new string? " << get<0>(haplotypes).size() - init_hap_size << endl;
                }
                else {
                    // cerr << "path seq emplaced (in forward):" << path_seq  << endl;
                    // int init_hap_size = get<0>(haplotypes).size(); // Note: just for debug purposes.
                    get<0>(haplotypes).emplace(path_seq);
                    // cerr << "was path_seq a copy? " << get<0>(haplotypes).size() - init_hap_size << endl;

                }
            }
        }
        // cerr << "haps in haplotypes: " << endl;
        // for (string hap : get<0>(haplotypes))
        // {
        //     cerr << hap << endl;
        // }
        // Align the new snarl:
        VG new_snarl = align_source_to_sink_haplotypes(get<0>(haplotypes));

        // count the number of bases in the snarl.
        new_snarl.for_each_handle([&](const handle_t handle) {
            error_record[5] += new_snarl.get_sequence(handle).size();
        });

        force_maximum_handle_size(new_snarl, _max_alignment_size);

        // integrate the new_snarl into the _graph, removing the old snarl as you go.
        // //todo: debug_statement
        // integrate_snarl(new_snarl, embedded_paths, sink_id, source_id);
        integrate_snarl(snarl, new_snarl, embedded_paths, source_id, sink_id, backwards);
    } else {
        if (!get<1>(haplotypes).empty()) {
            cerr << "found a snarl with source " << source_id << " and sink "
                 << sink_id
                 << " with haplotypes that start or end in the middle. Skipping." << endl;
            cerr << "There are " << sizeof(get<1>(haplotypes)) << " haplotypes of that description." << endl;
            // vector<string> string_haps = format_handle_haplotypes_to_strings(get<1>(haplotypes).front());
            // cerr << "First example: " << get<1>(haplotypes) << endl;
            error_record[1] = true;
        }
        if (get<0>(haplotypes).size() > _max_alignment_size) {
            cerr << "found a snarl with source " << source_id << " and sink "
                 << sink_id << " with too many haplotypes (" << get<0>(haplotypes).size()
                 << ") to efficiently align. Skipping." << endl;
            error_record[0] = true;
        }
        if (get<2>(haplotypes).size() != handles_in_snarl.size()) {
            cerr << "some handles in the snarl with source " << source_id
                 << " and sink " << sink_id
                 << " aren't accounted for by the gbwt_graph. "
                    "Skipping."
                 << endl;
            cerr << "size of snarl:" << handles_in_snarl.size() << "number of handles touched by gbwt graph: " << get<2>(haplotypes).size() << endl;
            cerr << "these handles are:" << endl << "\t";
            for (auto handle : handles_in_snarl) {
                if (get<2>(haplotypes).find(handle) == get<2>(haplotypes).end()) {
                    cerr << _graph.get_id(handle) << " ";
                }
            }
            cerr << endl;
            error_record[2] = true;
        }
    }
    // todo: decide if we should only normalize snarls that decrease in size.
    if (error_record[5] > error_record[4]) {
        cerr << "**************************in UNIT-TEST for normalize_snarl: **************************" << endl;
        cerr << "NOTE: normalized a snarl which *increased* in sequence quantity, "
                "with source "
             << source_id << endl
             << "\tsize before: " << error_record[4] << " size after: " << error_record[5]
             << endl;
    } else if (error_record[5] <= 0) {
        cerr << "normalized snarl size is <= zero: " << error_record[5] << endl;
    }
    return error_record;

}


// Given a vector of haplotypes of format vector< handle_t >, returns a vector of
// haplotypes of
//      format string (which is the concatenated sequences in the handles).
// Arguments:
//      _haploGraph: a gbwtgraph::GBWTGraph which contains the handles in vector< handle_t
//      > haplotypes. haplotypte_handle_vectors: a vector of haplotypes in vector<
//      handle_t > format.
// Returns: a vector of haplotypes of format string (which is the concatenated sequences
// in the handles).
unordered_set<string> SnarlNormalizer::format_handle_haplotypes_to_strings(
    const vector<vector<handle_t>> &haplotype_handle_vectors) {
    unordered_set<string> haplotype_strings;
    for (vector<handle_t> haplotype_handles : haplotype_handle_vectors) {
        string hap;
        for (handle_t &handle : haplotype_handles) {
            hap += _haploGraph.get_sequence(handle);
        }
        haplotype_strings.emplace(hap);
    }
    return haplotype_strings;
}

// TODO: eventually change to deal with haplotypes that start/end in middle of snarl.
// Aligns haplotypes to create a new _graph using MSAConverter's seqan converter.
//      Assumes that each haplotype stretches from source to sink.
// Arguments:
//      source_to_sink_haplotypes: a vector of haplotypes in string format (concat of
//      handle sequences).
// Returns:
//      VG object representing the newly realigned snarl.
VG SnarlNormalizer::align_source_to_sink_haplotypes(
    const unordered_set<string>& source_to_sink_haplotypes) {
    // cerr << "align_source_to_sink_haplotypes" << endl;
    // cerr << " haplotypes in source_to_sink_haplotypes: " << endl;
    // for (string hap : source_to_sink_haplotypes) {
    //     cerr << hap << endl;
    // }
    // cerr << "number of strings to align: " << source_to_sink_haplotypes.size() << endl;
    // TODO: make the following comment true, so that I can normalize haplotypes that
    // TODO:    aren't source_to_sink by adding a similar special character to strings in
    // TODO:    the middle of the snarl.
    // modify source_to_sink_haplotypes to replace the leading and
    // trailing character with a special character. This ensures that the leading char of
    // the haplotype becomes the first character in the newly aligned snarl's source - it
    // maintains the context of the snarl.

    // store the source/sink chars for later reattachment to source and sink.
    string random_element;
    for (auto hap : source_to_sink_haplotypes){
        random_element = hap;
        break;
    }
    string source_char(1, random_element.front());
    string sink_char(1, random_element.back());

    // cerr << "strings in path_seq before replacing final character: " << endl;
    // for (auto path : get<0>(haplotypes))
    // {
    //     cerr << path << endl;f
    // }

    // replace the source and sink chars with X, to force match at source and sink.
    unordered_set<string> edited_source_to_sink_haplotypes;
    // for (auto it = source_to_sink_haplotypes.begin(); it != source_to_sink_haplotypes.end(); it++)
    for (auto hap : source_to_sink_haplotypes)
    {
        // cerr << "hap before replace: " << hap << endl;
        hap.replace(0, 1, "X");
        hap.replace(hap.size() - 1, 1, "X");
        // cerr << "hap after replace: " << hap << endl;
        edited_source_to_sink_haplotypes.emplace(hap);
    }

    // //todo: debug_statement
    // source_to_sink_haplotypes.emplace_back("XX");

    // /// make a new scoring matrix with _match=5, _mismatch = -3, _gap_extend = -1, and
    // _gap_open = -3, EXCEPT that Q has to be matched with Q (so match score between Q
    // and Q =len(seq)+1)
    // // 1. Define type and constants.
    // //
    // // Define types for the score value and the scoring scheme.
    // typedef int TValue;
    // typedef seqan::Score<TValue, seqan::ScoreMatrix<seqan::Dna5, seqan::Default> >
    // TScoringScheme;
    // // Define our gap scores in some constants.
    // int const gapOpenScore = -1;
    // int const gapExtendScore = -1;

    // static int const _data[TAB_SIZE] =
    //     {
    //         1, 0, 0, 0, 0,
    //         0, 1, 0, 0, 0,
    //         0, 0, 1, 0, 0,
    //         0, 0, 0, 1, 0,
    //         0, 0, 0, 0, 0
    //     };

    // create seqan multiple_sequence_alignment object
    //// seqan::Align<seqan::DnaString>   align;
    seqan::Align<seqan::CharString> align;

    seqan::resize(rows(align), edited_source_to_sink_haplotypes.size());
    int i = 0;
    for (auto hap : edited_source_to_sink_haplotypes) {
        assignSource(row(align, i), hap.c_str());
        i++;
    }

    globalMsaAlignment(align, seqan::SimpleScore(5, -3, -1, -3));

    vector<string> row_strings;
    for (auto &row : rows(align)) {
        string row_string;
        auto it = begin(row);
        auto itEnd = end(row);
        for (; it != itEnd; it++) {
            row_string += *it;
        }
        // todo: debug_statement
        // cerr << "ROW_STRING: " << row_string << endl;
        // edit the row so that the proper source and sink chars are added to the
        // haplotype instead of the special characters added to ensure correct alignment
        // of source and sink.
        // cerr << "row_string before: " << row_string << endl;
        row_string.replace(0, 1, source_char);
        row_string.replace(row_string.size() - 1, 1, sink_char);
        row_strings.push_back(row_string);
        // cerr << "row_string after: " << row_string << endl;
    }

    stringstream ss;
    for (string seq : row_strings) {
        // todo: debug_statement
        // cerr << "seq in alignment:" << seq << endl;
        ss << endl << seq;
    }
    // ss << align;
    MSAConverter myMSAConverter = MSAConverter();
    myMSAConverter.load_alignments(ss, "seqan");
    VG snarl = myMSAConverter.make_graph();
    snarl.clear_paths();

    pair<vector<handle_t>, vector<handle_t>> source_and_sink =
        debug_get_sources_and_sinks(snarl);

    // TODO: throw exception(?) instead of cerr, or remove these messages if I'm confident
    // TODO:    code works.
    if (source_and_sink.first.size() != 1) {
        cerr << "WARNING! Snarl realignment has generated "
             << source_and_sink.first.size() << " source nodes." << endl;
    }

    if (source_and_sink.second.size() != 1) {
        cerr << "WARNING! Snarl realignment has generated "
             << source_and_sink.second.size() << " sink nodes." << endl;
    }

    return snarl;
}

/** For each handle in a given _graph, divides any handles greater than max_size into
 * parts that are equal to or less than the size of max_size.
 *
 * @param  {MutableHandleGraph} _graph : the _graph in which we want to force a maximum
 * handle size for all handles.
 * @param  {size_t} max_size          : the maximum size we want a handle to be.
 */
void SnarlNormalizer::force_maximum_handle_size(MutableHandleGraph &graph,
                                                const size_t &max_size) {
    // forcing each handle in the _graph to have a maximum sequence length of max_size:
    _graph.for_each_handle([&](handle_t handle) {
        // all the positions we want to make in the handle are in offsets.
        vector<size_t> offsets;

        size_t sequence_len = _graph.get_sequence(handle).size();
        int number_of_divisions = floor(sequence_len / max_size);

        // if the handle divides evenly into subhandles of size max_size, we don't need to
        // make the last cut (which would be at the very end of the handle - cutting off
        // no sequence).
        if (sequence_len % max_size == 0) {
            number_of_divisions--;
        }

        // calculate the position of all the divisions we want to make.
        for (int i = 1; i <= number_of_divisions; i++) {
            offsets.push_back(i * max_size);
        }

        // divide the handle into parts.
        _graph.divide_handle(handle, offsets);
    });
}

// TODO: change the arguments to handles, which contain orientation within themselves.
// Given a start and end node id, construct an extract subgraph between the two nodes
// (inclusive). Arguments:
//      _graph: a pathhandlegraph containing the snarl with embedded paths.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns:
//      a SubHandleGraph containing only the handles in _graph that are between start_id
//      and sink_id.
SubHandleGraph SnarlNormalizer::extract_subgraph(const HandleGraph &graph,
                                                 id_t source_id,
                                                 id_t sink_id,
                                                 const bool backwards) {
    // cerr << "extract_subgraph has source and sink: " << source_id << " " << sink_id << endl; 
    // because algorithm moves left to right, determine leftmost and rightmost nodes.
    id_t leftmost_id;
    id_t rightmost_id;
    // if snarl's "backwards," source is rightmost node, sink is leftmost.
    if (backwards) 
    {
        leftmost_id = sink_id;
        rightmost_id = source_id;
    }
    else 
    {
        leftmost_id = source_id;
        rightmost_id = sink_id;
    }
    // cerr << "extract_subgraph" << endl;
    /// make a subgraph containing only nodes of interest. (e.g. a snarl)
    // make empty subgraph
    SubHandleGraph subgraph = SubHandleGraph(&graph);

    unordered_set<id_t> visited;  // to avoid counting the same node twice.
    unordered_set<id_t> to_visit; // nodes found that belong in the subgraph.

    // initialize with leftmost_handle (because we move only to the right of leftmost_handle):
    handle_t leftmost_handle = _graph.get_handle(leftmost_id);
    subgraph.add_handle(leftmost_handle);
    visited.insert(graph.get_id(leftmost_handle));

    // look only to the right of leftmost_handle
    _graph.follow_edges(leftmost_handle, false, [&](const handle_t &handle) {
        // mark the nodes to come as to_visit
        if (visited.find(graph.get_id(handle)) == visited.end()) {
            to_visit.insert(graph.get_id(handle));
        }
    });

    /// explore the rest of the snarl:
    while (to_visit.size() != 0) {
        // remove cur_handle from to_visit
        unordered_set<id_t>::iterator cur_index = to_visit.begin();
        handle_t cur_handle = _graph.get_handle(*cur_index);

        to_visit.erase(cur_index);

        /// visit cur_handle
        visited.insert(graph.get_id(cur_handle));

        subgraph.add_handle(cur_handle);

        if (graph.get_id(cur_handle) != rightmost_id) { // don't iterate past rightmost node!
            // look for all nodes connected to cur_handle that need to be added
            // looking to the left,
            _graph.follow_edges(cur_handle, true, [&](const handle_t &handle) {
                // mark the nodes to come as to_visit
                if (visited.find(graph.get_id(handle)) == visited.end()) {
                    to_visit.insert(graph.get_id(handle));
                }
            });
            // looking to the right,
            _graph.follow_edges(cur_handle, false, [&](const handle_t &handle) {
                // mark the nodes to come as to_visit
                if (visited.find(graph.get_id(handle)) == visited.end()) {
                    to_visit.insert(graph.get_id(handle));
                }
            });
        }
    }
    return subgraph;
}

// Integrates the snarl into the _graph, replacing the snarl occupying the space between
// source_id and sink_id.
//      In the process, transfers any embedded paths traversing the old snarl into the new
//      snarl.
// Arguments:
//      _graph: the _graph in which we want to insert the snarl.
//      to_insert_snarl: a *separate* handle_graph from _graph, often generated from
//      MSAconverter. embedded_paths: a vector of paths, where each is a pair.
//                        pair.first is the first step_handle of interest in the
//                        old_embedded_path, and pair.second is the step_handle *after*
//                        the last step_handle of interest in the old_embedded_path (can
//                        be the null step at the end of the path.)
//                        Note: these paths will be altered to represent the way they
//                        overlap in the new snarl. Otherwise, they would be invalidated.
//      source_id: the source of the old (to be replaced) snarl in _graph
//      sink_id: the sink of the old (to be replaced) snarl in _graph.
// Return: None.
void SnarlNormalizer::integrate_snarl(SubHandleGraph &old_snarl, 
    const HandleGraph &to_insert_snarl,
    vector<pair<step_handle_t, step_handle_t>>& embedded_paths, 
    const id_t &source_id, const id_t &sink_id, const bool backwards) {
    // cerr << "integrate_snarl" << endl;

    //todo: debug_statement
    // cerr << "\nhandles in to_insert_snarl:" << endl;
    // to_insert_snarl.for_each_handle([&](const handle_t &handle) {
    //     cerr << to_insert_snarl.get_id(handle) << " "
    //          << to_insert_snarl.get_sequence(handle) << " ";
    //     cerr << "neighbors: ";
    //     to_insert_snarl.follow_edges(handle, false, [&](const handle_t &next) {
    //         cerr << "     " << to_insert_snarl.get_id(next) << endl;
    //     });
    //     cerr << " \n";
    // });
    // cerr << endl;
    // Get old _graph snarl
    // SubHandleGraph old_snarl = extract_subgraph(_graph, source_id, sink_id, backwards);

    // TODO: debug_statement: Check to make sure that newly made snarl has only one start
    // and end.
    // TODO:     (shouldn't be necessary once we've implemented alignment with
    // leading/trailing special chars.) Identify old and new snarl start and sink
    pair<vector<handle_t>, vector<handle_t>> to_insert_snarl_defining_handles =
        debug_get_sources_and_sinks(to_insert_snarl);

    if (to_insert_snarl_defining_handles.first.size() > 1 ||
        to_insert_snarl_defining_handles.second.size() > 1) {
        cerr << "ERROR: newly made snarl from a snarl with source " << source_id
             << " has more than one start or end. # of starts: "
             << to_insert_snarl_defining_handles.first.size()
             << " # of ends: " << to_insert_snarl_defining_handles.second.size() << endl;
        return;
    }

    /// Replace start and end handles of old _graph snarl with to_insert_snarl start and
    /// end, and delete rest of old _graph snarl:

    // add to_insert_snarl into _graph without directly attaching the snarl to the _graph
    // (yet).
    vector<handle_t> to_insert_snarl_topo_order =
        handlealgs::lazier_topological_order(&to_insert_snarl);

    // Construct a parallel new_snarl_topo_order to identify
    // paralogous nodes between to_insert_snarl and the new snarl inserted in _graph.
    vector<handle_t> new_snarl_topo_order;

    // integrate the handles from to_insert_snarl into the _graph, and keep track of their
    // identities by adding them to new_snarl_topo_order.
    for (handle_t to_insert_snarl_handle : to_insert_snarl_topo_order) {
        // //todo: debug_statement:
        // cerr << " pre-inserted snarl handle: "
        //      << to_insert_snarl.get_id(to_insert_snarl_handle) << " "
        //      << to_insert_snarl.get_sequence(to_insert_snarl_handle) << endl;

        handle_t graph_handle =
            _graph.create_handle(to_insert_snarl.get_sequence(to_insert_snarl_handle));
        new_snarl_topo_order.push_back(graph_handle);
        // cerr << "graph handle being inserted into new_snarl_topo_order:" << _graph.get_id(graph_handle) << endl;
    }

    // Connect the newly made handles in the _graph together the way they were connected
    // in to_insert_snarl:
    for (int i = 0; i < to_insert_snarl_topo_order.size(); i++) {
        to_insert_snarl.follow_edges(
            to_insert_snarl_topo_order[i], false, [&](const handle_t &snarl_handle) {
                // get topo_index of nodes to be connected to _graph start handle
                auto it = find(to_insert_snarl_topo_order.begin(),
                               to_insert_snarl_topo_order.end(), snarl_handle);
                int topo_index = it - to_insert_snarl_topo_order.begin();

                // connect _graph start handle
                _graph.create_edge(new_snarl_topo_order[i],
                                   new_snarl_topo_order[topo_index]);
            });
    }

    // save the source and sink values of new_snarl_topo_order, since topological order is
    // not necessarily preserved by move_path_to_snarl. Is temporary b/c we need to
    // replace the handles with ones with the right id_t label for source and sink later
    // on.
    id_t temp_snarl_leftmost_id = _graph.get_id(new_snarl_topo_order.front());
    id_t temp_snarl_rightmost_id = _graph.get_id(new_snarl_topo_order.back());
    // cerr << "the temp source id: " << temp_snarl_leftmost_id << endl;
    // cerr << "the temp sink id: " << temp_snarl_rightmost_id << endl;

    // Add the neighbors of the source and sink of the original snarl to the new_snarl's
    // source and sink.
    // source integration:
    if (!backwards)
    {
    _graph.follow_edges(
        _graph.get_handle(source_id), true, [&](const handle_t &prev_handle) {
            _graph.create_edge(prev_handle, _graph.get_handle(temp_snarl_leftmost_id));
        });
    _graph.follow_edges(
        _graph.get_handle(sink_id), false, [&](const handle_t &next_handle) {
            _graph.create_edge(_graph.get_handle(temp_snarl_rightmost_id), next_handle);
        });
    }
    else 
    {
        _graph.follow_edges(
        _graph.get_handle(source_id), false, [&](const handle_t &next_handle) {
            _graph.create_edge(_graph.get_handle(temp_snarl_rightmost_id), next_handle);
        });
    _graph.follow_edges(
        _graph.get_handle(sink_id), true, [&](const handle_t &prev_handle) {
            _graph.create_edge(prev_handle, _graph.get_handle(temp_snarl_leftmost_id));
        });
    }
    // For each path of interest, move it onto the new_snarl.
    for (int i = 0; i != embedded_paths.size(); i++)
    {
        // //todo: debug_statement
        // cerr << "the new sink id: " << temp_snarl_rightmost_id << endl;
        // //todo: debug_statement
        // move_path_to_snarl(path, new_snarl_topo_order, temp_snarl_rightmost_id,
        //                    temp_snarl_leftmost_id, sink_id, source_id);
        // move_path_to_snarl(path, new_snarl_topo_order, temp_snarl_leftmost_id,
        //                    temp_snarl_rightmost_id, source_id, sink_id, backwards);
        // cerr << "is path backwards? " << backwards << endl;
        // cerr << "path first: " << _graph.get_id(_graph.get_handle_of_step(path.first)) << " step after path first: " << _graph.get_id(_graph.get_handle_of_step(_graph.get_next_step(path.first))) << " path second: " << _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) << endl;
        // cerr << "source: " << source_id << " sink: " << sink_id << endl;
        // pair<bool, bool> path_spans_left_right;
        // path_spans_left_right.first = (!backwards && _graph.get_id(_graph.get_handle_of_step(path.first)) == source_id) || (backwards && _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) == source_id);
        // path_spans_left_right.second = (!backwards && _graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(path.second))) == sink_id) || (backwards && _graph.get_id(_graph.get_handle_of_step(path.first)) == sink_id);
        // cerr << "first: " << path_spans_left_right.first << "second: " << path_spans_left_right.second << endl;
        pair<bool, bool> path_spans_left_right;
        path_spans_left_right.first = (_graph.get_id(_graph.get_handle_of_step(embedded_paths[i].first)) == source_id);
        path_spans_left_right.second = (_graph.get_id(_graph.get_handle_of_step(_graph.get_previous_step(embedded_paths[i].second))) == sink_id);

        embedded_paths[i] = move_path_to_new_snarl(embedded_paths[i], temp_snarl_leftmost_id, temp_snarl_rightmost_id, path_spans_left_right, !backwards, make_pair(source_id, sink_id));
    }

    // Destroy the old snarl.
    old_snarl.for_each_handle([&](const handle_t &handle) 
    {
        // //todo: debug_statement these are the handles in old_snarl:
        // cerr << old_snarl.get_id(handle) << old_snarl.get_sequence(handle) << endl;
        _graph.destroy_handle(handle);
    });

    // Replace the source and sink handles with ones that have the original source/sink id
    // (for compatibility with future iterations on neighboring top-level snarls using the
    // same snarl manager. Couldn't replace it before b/c we needed the old handles to
    // move the paths.
    handle_t new_leftmost_handle;
    handle_t new_rightmost_handle;
    if (!backwards) 
    {
        new_leftmost_handle = overwrite_node_id(temp_snarl_leftmost_id, source_id);
        new_rightmost_handle = overwrite_node_id(temp_snarl_rightmost_id, sink_id);
    }
    else
    {
        new_leftmost_handle = overwrite_node_id(temp_snarl_leftmost_id, sink_id);
        new_rightmost_handle = overwrite_node_id(temp_snarl_rightmost_id, source_id);
    }    
}


/**
 * Deletes the given handle's underlying node, and returns a new handle to a new node 
 * with the desired node_id
 * 
 * @param  {id_t} handle     : The old node id, to be replaced with a new node id.
 * @param  {id_t} node_id    : The node id for the new node. Cannot be currently in use in
 *                              the graph.
 * @return {handle_t}        : The new handle, in the same position as the original handle
 *                              in the graph, but with the new node_id.
 */
handle_t SnarlNormalizer::overwrite_node_id(const id_t& old_node_id, const id_t& new_node_id)
{
    handle_t old_handle = _graph.get_handle(old_node_id);
    handle_t new_handle = _graph.create_handle(_graph.get_sequence(old_handle), new_node_id);

    // move the edges:
    _graph.follow_edges(old_handle, true, [&](const handle_t &prev_handle) 
    {
        _graph.create_edge(prev_handle, new_handle);
    });
    _graph.follow_edges(old_handle, false, [&](const handle_t &next_handle)
    {
        _graph.create_edge(new_handle, next_handle);
    });

    // move the paths:
    _graph.for_each_step_on_handle(old_handle, [&](step_handle_t step) 
    {
        handle_t properly_oriented_old_handle = _graph.get_handle_of_step(step); 
        if (_graph.get_is_reverse(properly_oriented_old_handle) != _graph.get_is_reverse(new_handle))
        {
            new_handle = _graph.flip(new_handle);
        }
        _graph.rewrite_segment(step, _graph.get_next_step(step), vector<handle_t>{new_handle});
    });

    // delete the old_handle:
    _graph.destroy_handle(old_handle);
    return new_handle;
}
/** Used to help move_path_to_snarl map paths from an old snarl to its newly
 * normalized counterpart. In particular, ensures that any paths which touch the
 * source and/or sink of the old snarl still do so in the new snarl (which is
 * important to ensure that we don't break any paths partway through the snarl.)
 *
 * @param  {HandleGraph} _graph         : the _graph that contains the old and new snarl
 * nodes.
 * @param  {id_t} new_source_id        : the node id of the newly created source.
 * @param  {id_t} new_sink_id          : the node id of the newly created sink.
 * @param  {bool} touching_source      : true if the path is connected to the old
 * source.
 * @param  {bool} touching_sink        : true if the path is connected to the old
 * sink.
 * @param  {handle_t} path_start : proposed source for the path in the new snarl.
 * @param  {handle_t} path_end   : proposed sink for the path in the new snarl.
 * @return {bool}                      : true if the path satisfies the requirement
 * that, if the original path covered the old source or sink, the new path also covers
 * the same respective nodes in the new snarl.
 */
bool SnarlNormalizer::source_and_sink_handles_map_properly(
    const HandleGraph &graph, const id_t &new_source_id, const id_t &new_sink_id,
    const bool &touching_source, const bool &touching_sink, const handle_t &path_start,
    const handle_t &path_end) {

    bool path_map = false;
    // cerr << "touching source? " << touching_source << "touching_sink" << touching_sink
    //      << "source is source?" << (graph.get_id(path_start) == new_source_id)
    //      << " sink is sink: " << (graph.get_id(path_end) == new_sink_id) << endl;
    if (touching_source && touching_sink) {
        path_map = ((graph.get_id(path_start) == new_source_id) &&
                    (graph.get_id(path_end) == new_sink_id));
    } else if (touching_source) {
        path_map = (graph.get_id(path_start) == new_source_id);
    } else if (touching_sink) {
        path_map = (graph.get_id(path_end) == new_sink_id);
    } else {
        path_map = true;
    }
    // cerr << "path_map " << path_map << endl;
    return path_map;
}

// Determines whether some subsequence in a handle satisfies the condition of being
// the beginning of a path.
//      If the path_seq is longer than the handle_seq, only checks subsequences that
//      reach from the beginning/middle of the handle_seq to the end. If path_seq is
//      shorter than handle_seq, checks for any substring of length path_seq within
//      the handle_seq, as well as substrings smaller than length path_seq that extend
//      beyond the current handle.
// Arguments:
//      handle_seq: the sequence in the handle we're trying to identify as a
//      start_of_path_seq. path_seq: the sequence in the path we're trying to find
//      starting points for in handle_seq
// Return: a vector of all potential starting index of the subsequence in the
// handle_seq.
vector<int> SnarlNormalizer::check_handle_as_start_of_path_seq(const string &handle_seq,
                                                               const string &path_seq) {
    vector<int> possible_start_indices;
    // If the handle_seq.size <= path_seq.size, look for subsequences reaching from
    // beginning/middle of handle_seq to the end - where path_seq may run off the end
    // of this handle to the next in the snarl.
    if (handle_seq.size() <= path_seq.size()) {
        // iterate through all possible starting positions in the handle_seq.
        for (int handle_start_i = 0; handle_start_i < handle_seq.size();
             handle_start_i++) {
            int subseq_size = handle_seq.size() - handle_start_i;
            // The path_seq subsequence of interest is from 0 to subseq_size;
            // The handle_seq subsequence of interest starts at handle_start_i
            // and ends at the end of the handle_seq (len subseq_size).
            // if compare returns 0, the substring matches.
            if (path_seq.compare(0, subseq_size, handle_seq, handle_start_i,
                                 subseq_size) == 0) {
                possible_start_indices.push_back(handle_start_i);
            }
        }
    }
    // if handle_seq.size > path_seq.size, look for any subsequence within handle_seq
    // of path_seq.size, as well as any subsequence smaller than path_seq reaching
    // from middle of handle_seq to the end of handle_seq.
    else {
        // first, search through all handle_seq for any comparable subsequence of
        // path_seq.size. Note: only differences between this for loop and above for
        // loop is that handle_start_i stops at (<= path_seq.size() -
        // handle_seq.size()), and subseq.size() = path_seq.size()
        for (int handle_start_i = 0;
             handle_start_i <= (handle_seq.size() - path_seq.size()); handle_start_i++) {
            int subseq_size = path_seq.size();
            // The path_seq subsequence of interest is from 0 to subseq_size;
            // The handle_seq subsequence of interest starts at handle_start_i
            // and ends at the end of the handle_seq (len subseq_size).
            // if compare returns 0, the substring matches.
            if (path_seq.compare(0, subseq_size, handle_seq, handle_start_i,
                                 subseq_size) == 0) {
                possible_start_indices.push_back(handle_start_i);
            }
        }
        // second, search through the last few bases of handle_seq for the beginning
        // of path_seq. Note: nearly identical for loop to the one in "if
        // (handle_seq.size()
        // <= path_seq.size())"
        for (int handle_start_i = (handle_seq.size() - path_seq.size() + 1);
             handle_start_i < handle_seq.size(); handle_start_i++) {
            int subseq_size = handle_seq.size() - handle_start_i;
            // The path_seq subsequence of interest is from 0 to subseq_size;
            // The handle_seq subsequence of interest starts at handle_start_i
            // and ends at the end of the handle_seq (len subseq_size).
            // if compare returns 0, the substring matches.
            if (path_seq.compare(0, subseq_size, handle_seq, handle_start_i,
                                 subseq_size) == 0) {
                possible_start_indices.push_back(handle_start_i);
            }
        }
    }
    // Note: if we passed through the above check without returning anything, then
    // there isn't any satisfactory subsequence and we'll return an empty vector.
    return possible_start_indices;
}

// ------------------------------ DEBUG CODE BELOW:
// ------------------------------------------

// Returns pair where pair.first is a vector of all sources of the given _graph and
// path.second is all the sinks of the given _graph. If _graph is a subhandlegraph of a
// snarl, there should only be one source and sink each.
pair<vector<handle_t>, vector<handle_t>>
SnarlNormalizer::debug_get_sources_and_sinks(const HandleGraph &graph) {
    // cerr << "debug_get_source_and_sinks" << endl;
    vector<handle_t> sink;
    vector<handle_t> source;

    // identify sources and sinks
    graph.for_each_handle([&](const handle_t &handle) {
        //todo: debug_statements in code below:
        // cerr << "identifying if " << graph.get_id(handle) << "is a source/sink." <<endl;
        bool is_source = true, is_sink = true;
        // cerr << "handles to the left: ";
        graph.follow_edges(handle, true, [&](const handle_t &prev) {
            // cerr << graph.get_id(prev) << endl;
            is_source = false;
            return false;
        });
        // cerr << "handles to the right: ";
        graph.follow_edges(handle, false, [&](const handle_t &next) {
            // cerr << graph.get_id(next) << endl;
            is_sink = false;
            return false;
        });

        if (is_source) {
            // cerr<< "determined is_source" << endl;
            source.push_back(handle);
        }
        if (is_sink) {
            // cerr<< "determined is_sink" << endl;
            sink.emplace_back(handle);
        }
    });
    return pair<vector<handle_t>, vector<handle_t>>(source, sink);
}

}
}