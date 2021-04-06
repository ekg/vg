// TODO: I remove snarls where a haplotype begins/ends in the middle
// TODO:    of the snarl. Get rid of this once alignment issue is addressed!
// TODO: also, limits the number of haplotypes to be aligned, since snarl starting at
// TODO:    2049699 with 258 haplotypes is taking many minutes.

// TODO:    another had 146 haplotypes and took maybe 5 minutes to align. (kept that one
// in tho' )
#pragma once // TODO: remove this, to avoid warnings + maybe bad coding practice?
#include "0_draft_haplotype_realignment.hpp"

#include <algorithm>
#include <string>

#include <seqan/align.h>
#include <seqan/graph_align.h>
#include <seqan/graph_msa.h>

#include "../gbwt_helper.hpp"
#include "../handle.hpp"
#include "../msa_converter.hpp"
#include "../snarls.hpp"
#include "../vg.hpp"
#include "is_acyclic.hpp"
#include <gbwtgraph/gbwtgraph.h>

#include "../types.hpp"
#include "extract_containing_graph.hpp"

namespace vg {

// TODO: allow for snarls that have haplotypes that begin or end in the middle of the
//      snarl
// Runs disambiguate_snarl on every top-level snarl in the graph, so long as the
//      snarl only contains haplotype threads that extend fully from source to sink.
// Arguments:
//      graph: the full-sized handlegraph that will undergo edits in a snarl.
//      haploGraph: the corresponding gbwtgraph::GBWTGraph of graph.
//      snarl_stream: the file stream from .snarl file corresponding to graph.
void disambiguate_top_level_snarls(MutablePathDeletableHandleGraph &graph,
                                   const gbwtgraph::GBWTGraph &haploGraph, ifstream &snarl_stream,
                                   const int &max_alignment_size) {
    // cerr << "disambiguate_top_level_snarls" << endl;
    SnarlManager *snarl_manager = new SnarlManager(snarl_stream);

    int num_snarls_normalized = 0;
    int num_snarls_skipped = 0;
    vector<const Snarl *> snarl_roots = snarl_manager->top_level_snarls();
    // error_record's bools are:
    //      <snarl exceeds max number of threads that can be efficiently aligned,
    //        snarl has haplotypes starting/ending in the middle,
    //        some handles in the snarl aren't connected by a thread,
    //        snarl is cyclic>
    tuple<bool, bool, bool, bool, int, int> one_snarl_error_record;
    tuple<int, int, int, int> full_error_record;
    int num_of_errors = 4;

    pair<int, int> snarl_sequence_change;

    for (auto roots : snarl_roots) {

        if (roots->start().node_id() == 42777) {
        cerr << "disambiguating snarl #" << (num_snarls_normalized + num_snarls_skipped)
             << " source: " << roots->start().node_id()
             << " sink: " << roots->end().node_id() << endl;
        one_snarl_error_record =
            disambiguate_snarl(graph, haploGraph, roots->start().node_id(),
                               roots->end().node_id(), max_alignment_size);
        get<0>(full_error_record) += get<0>(one_snarl_error_record);
        get<1>(full_error_record) += get<1>(one_snarl_error_record);
        get<2>(full_error_record) += get<2>(one_snarl_error_record);
        get<3>(full_error_record) += get<3>(one_snarl_error_record);
        if (!(get<0>(one_snarl_error_record) || get<1>(one_snarl_error_record) ||
              get<2>(one_snarl_error_record) || get<3>(one_snarl_error_record))) {
            num_snarls_normalized += 1;
            snarl_sequence_change.first += get<4>(one_snarl_error_record);
            snarl_sequence_change.second += get<5>(one_snarl_error_record);
        } else {
            num_snarls_skipped += 1;
        }
        }
    }
    cerr << endl
         << "normalized " << num_snarls_normalized << " snarl(s), skipped "
         << num_snarls_skipped << " snarls because. . .\nthey exceeded the size limit ("
         << get<0>(full_error_record)
         << "snarls),\nhad haplotypes starting/ending in the middle of the snarl ("
         << get<1>(full_error_record) << "),\nthe snarl was cyclic ("
         << get<3>(full_error_record)
         << " snarls),\nor there "
            "were handles not connected by the gbwt info ("
         << get<2>(full_error_record) << " snarls)." << endl;
    cerr << "amount of sequence in normalized snarls before normalization: " << snarl_sequence_change.first << endl;
    cerr << "amount of sequence in normalized snarls after normalization: " << snarl_sequence_change.second << endl;

    /// Args:
    ///  source                 graph to extract subgraph from
    ///  into                   graph to extract into
    ///  positions              search outward from these positions
    ///  max_dist               include all nodes and edges that can be reached in at most
    ///  this distance reversing_walk_length  also find graph material that can be reached

    // //todo: debug_statement
    // VG outGraph;
    // pos_t source_pos = make_pos_t(4211565, false, 0);
    // vector<pos_t> pos_vec;
    // pos_vec.push_back(source_pos);
    // algorithms::extract_containing_graph(&graph, &outGraph, pos_vec, 150);
    // outGraph.serialize_to_ostream(cout);

    delete snarl_manager;
}

// For a snarl in the given graph, with every edge covered by at least one haplotype
// thread in the gbwtgraph::GBWTGraph,
//      extract all sequences in the snarl corresponding to the haplotype threads and
//      re-align them with MSAConverter/seqan to form a new snarl. Embedded paths are
//      preserved; GBWT haplotypes in the snarl are not conserved.
// Arguments:
//      graph: the full-sized handlegraph that will undergo edits in a snarl.
//      haploGraph: the corresponding gbwtgraph::GBWTGraph of graph.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns: none.
// TODO: allow for snarls that have haplotypes that begin or end in the middle of the
// snarl.
tuple<bool, bool, bool, bool, int, int>
disambiguate_snarl(MutablePathDeletableHandleGraph &graph, const gbwtgraph::GBWTGraph &haploGraph,
                   const id_t &source_id, const id_t &sink_id,
                   const int &max_alignment_size) {
    // cerr << "disambiguate_snarl" << endl;
    // error_record's bools are:
    //      <snarl exceeds max number of threads that can be efficiently aligned,
    //        snarl has haplotypes starting/ending in the middle,
    //        some handles in the snarl aren't connected by a thread,
    //        snarl is cyclic,
    //        size of original snarl,
    //        size of new snarl.>
    tuple<bool, bool, bool, bool, int, int> error_record{0, 0, 0, 0, 0, 0};
    SubHandleGraph snarl = extract_subgraph(graph, source_id, sink_id);

    if (!algorithms::is_acyclic(&snarl)) {
        cerr << "snarl at " << source_id << " is cyclic. Skipping." << endl;
        get<3>(error_record) = true;
    }

    // First, find all haplotypes encoded by the GBWT, in order to create the new snarl.
    // Return value is tuple< haplotypes_that_stretch_from_source_to_sink,
    // haplotypes_that_end/start_prematurely, set of all handles in the haplotypes >
    tuple<vector<vector<handle_t>>, vector<vector<handle_t>>, unordered_set<handle_t>>
        haplotypes = extract_gbwt_haplotypes(snarl, haploGraph, source_id, sink_id);

    // check to make sure that the gbwt graph has threads connecting all handles:
    // ( needs the unordered_set from extract_gbwt haplotypes to be equal to the number of
    // handles in the snarl).
    unordered_set<handle_t> handles_in_snarl;
    snarl.for_each_handle([&](const handle_t handle) {
        handles_in_snarl.emplace(handle);
        // count the number of bases in the snarl.
        get<4>(error_record) += snarl.get_sequence(handle).size();
    });

    // TODO: this if statement removes snarls where a haplotype begins/ends in the middle
    // TODO:    of the snarl. Get rid of this once alignment issue is addressed!
    // TODO: also, limits the number of haplotypes to be aligned, since snarl starting at
    // TODO:    2049699 with 258 haplotypes is taking many minutes.
    if (get<1>(haplotypes).empty() && get<0>(haplotypes).size() < max_alignment_size &&
        get<2>(haplotypes).size() == handles_in_snarl.size()) {
        // if (get<1>(haplotypes).empty() && get<2>(haplotypes).size() ==
        // handles_in_snarl) { if (get<1>(haplotypes).empty()) { Convert the haplotypes
        // from vector<handle_t> format to string format.
        vector<string> haplotypes_from_source_to_sink =
            format_handle_haplotypes_to_strings(haploGraph, get<0>(haplotypes));
        // vector< string > other_haplotypes =
        // format_handle_haplotypes_to_strings(haploGraph, get<1>(haplotypes));

        // Get the embedded paths in the snarl out of the graph, for the purposes of
        // moving them into the new snarl. In addition, any embedded paths that stretch
        // from source to sink are aligned in the new snarl.
        // TODO: once haplotypes that begin/end in the middle of the snarl have been
        // TODO:    accounted for in the code, align all embedded paths? (and remove next
        // TODO:    chunk of code that finds source-to-sink paths)?
        vector<pair<step_handle_t, step_handle_t>> embedded_paths =
            extract_embedded_paths_in_snarl(graph, source_id, sink_id);

        // find the paths that stretch from source to sink:
        for (auto path : embedded_paths) {
            // cerr << "checking path of name " <<
            // graph.get_path_name(graph.get_path_handle_of_step(path.first)) << " with
            // start " << graph.get_id(graph.get_handle_of_step(path.first)) << " and sink
            // " <<
            // graph.get_id(graph.get_handle_of_step(graph.get_previous_step(path.second)))
            // << endl;
            if (graph.get_id(graph.get_handle_of_step(path.first)) == source_id &&
                graph.get_id(graph.get_handle_of_step(
                    graph.get_previous_step(path.second))) == sink_id) {
                // cerr << "adding path of name " <<
                // graph.get_path_name(graph.get_path_handle_of_step(path.first)) << endl;
                // get the sequence of the source to sink path, and add it to the paths to
                // be aligned.
                string path_seq;
                step_handle_t cur_step = path.first;
                while (cur_step != path.second) {
                    path_seq += graph.get_sequence(graph.get_handle_of_step(cur_step));
                    cur_step = graph.get_next_step(cur_step);
                }
                haplotypes_from_source_to_sink.push_back(path_seq);
            }
        }
        // Align the new snarl:
        VG new_snarl = align_source_to_sink_haplotypes(haplotypes_from_source_to_sink);

        // count the number of bases in the snarl.
        new_snarl.for_each_handle([&](const handle_t handle) {
            get<5>(error_record) += new_snarl.get_sequence(handle).size();
        });

        // todo: make 32 a part of the general object maximum handle_size info.
        force_maximum_handle_size(new_snarl, 32);

        // todo: debug_statement
        // new_snarl.for_each_handle([&](const handle_t& handle) {
        //     cerr << new_snarl.get_id(handle) << " " << new_snarl.get_sequence(handle)
        //     << "\t";
        // });

        // integrate the new_snarl into the graph, removing the old snarl as you go.
        integrate_snarl(graph, new_snarl, embedded_paths, source_id, sink_id);
        return error_record;
    } else {
        if (!get<1>(haplotypes).empty()) {
            cerr << "found a snarl starting at " << source_id << " and ending at "
                 << sink_id
                 << " with haplotypes that start or end in the middle. Skipping." << endl;
            get<1>(error_record) = true;
        }
        if (get<0>(haplotypes).size() > max_alignment_size) {
            cerr << "found a snarl starting at " << source_id << " and ending at "
                 << sink_id << " with too many haplotypes (" << get<0>(haplotypes).size()
                 << ") to efficiently align. Skipping." << endl;
            get<0>(error_record) = true;
        }
        if (get<2>(haplotypes).size() != handles_in_snarl.size()) {
            cerr << "some handles in the snarl starting at " << source_id
                 << " and ending at " << sink_id
                 << " aren't accounted for by the gbwt graph. "
                    "Skipping."
                 << endl;
            cerr << "these handles are:" << endl << "\t";
            for (auto handle : handles_in_snarl) {
                if (get<2>(haplotypes).find(handle) == get<2>(haplotypes).end()) {
                    cerr << graph.get_id(handle) << " ";
                }
            }
            cerr << endl;
            get<2>(error_record) = true;
        }
        if (get<5>(error_record) > get<4>(error_record)){
            cerr << "NOTE: normalized a snarl which *increased* in sequence quantity, rather than decreased." << endl;
        }
        return error_record;
    }
} // namespace vg

// TODO: test that it successfully extracts any haplotypes that start/end in the middle of
// TODO:    the snarl.
// For a snarl in a given gbwtgraph::GBWTGraph, extract all the haplotypes in the snarl. Haplotypes
// are represented
//      by vectors of handles, representing the chain of handles in a thread.
// Arguments:
//      haploGraph: the gbwtgraph::GBWTGraph containing the snarl.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns:
//      a pair containting two sets of paths (each represented by a vector<handle_t>). The
//      first in the pair represents all paths reaching from source to sink in the snarl,
//      and the second representing all other paths in the snarl (e.g. any that don't
//      reach both source and sink in the graph.)
// pair<vector<vector<handle_t>>, vector<vector<handle_t>>>
tuple<vector<vector<handle_t>>, vector<vector<handle_t>>, unordered_set<handle_t>>
extract_gbwt_haplotypes(const SubHandleGraph &snarl, const gbwtgraph::GBWTGraph &haploGraph,
                        const id_t &source_id, const id_t &sink_id) {
    // cerr << "extract_gbwt_haplotypes" << endl;

    // haplotype_queue contains all started exon_haplotypes not completed yet.
    // Every time we encounter a branch in the paths, the next node down the path
    // Is stored here, along with the vector of handles that represents the path up
    // to the SearchState.
    vector<pair<vector<handle_t>, gbwt::SearchState>> haplotype_queue;

    // source and sink handle for haploGraph:
    handle_t source_handle = haploGraph.get_handle(source_id);
    handle_t sink_handle = haploGraph.get_handle(sink_id);

    // place source in haplotype_queue.
    vector<handle_t> source_handle_vec(1, source_handle);
    gbwt::SearchState source_state = haploGraph.get_state(source_handle);
    haplotype_queue.push_back(make_pair(source_handle_vec, source_state));

    // touched_handles contains all handles that have been touched by the
    // depth first search below, for later use in other_haplotypes_to_strings, which
    // identifies paths that didn't stretch from source to sink in the snarl.
    unordered_set<handle_t> touched_handles{source_handle, sink_handle};

    // haplotypes contains all "finished" haplotypes - those that were either walked
    // to their conclusion, or until they reached the sink.
    vector<vector<handle_t>> haplotypes_from_source_to_sink;
    vector<vector<handle_t>> other_haplotypes;

    // sometimes a gbwt thread will indicate a connection between two handles that doesn't
    // actually exist in the graph. These connections need to be ignored.
    unordered_set<edge_t> incorrect_connections;

    // int prev_size = 0;
    // for every partly-extracted thread, extend the thread until it either reaches
    // the sink of the snarl or the end of the thread.
    while (!haplotype_queue.empty()) {
        // todo: debug_statement
        // cerr << "haplotype queue: ";
        // cerr << "size of queue:" << haplotype_queue.size() << " " << endl;
        // for (auto hap : haplotype_queue) {
        //     cerr << "size: " << hap.first.size() << endl << "handle_ids: ";
        //     for (handle_t handle : hap.first) {
        //         cerr << haploGraph.get_id(handle) << " ";
        //     }
        //     cerr << endl;
        // }

        // get a haplotype out of haplotype_queue to extend -
        // a tuple of (handles_traversed_so_far, last_touched_SearchState)
        pair<vector<handle_t>, gbwt::SearchState> cur_haplotype = haplotype_queue.back();
        haplotype_queue.pop_back();

        // get all the subsequent search_states that immediately follow the searchstate
        // from cur_haplotype.
        vector<gbwt::SearchState> next_searches;
        haploGraph.follow_paths(cur_haplotype.second,
                                [&](const gbwt::SearchState next_search) -> bool {
                                    next_searches.push_back(next_search);
                                    return true;
                                });

        // if next_searches > 1, then we need to make multiple new haplotypes to be
        // recorded in haplotype_queue or one of the finished haplotype_handle_vectors.
        if (next_searches.size() > 1) {
            // for every next_search in next_searches, either create a new, extended
            // cur_haplotype to push into haplotype queue, or place in the
            // haplotypes_from_source_to_sink if haplotype extends to sink, or place in
            // the other_haplotypes if haplotype ends before reaching sink.
            for (gbwt::SearchState next_search : next_searches) {
                handle_t next_handle = haploGraph.node_to_handle(next_search.node);
                // if (!snarl.has_node(snarl.get_id(next_handle)) &&
                // make_pair(haploGraph.get_id(cur_haplotype.first.back()),haploGraph.get_id(next_handle)))
                // {
                if (!snarl.has_edge(cur_haplotype.first.back(), next_handle)) {
                    if (incorrect_connections.find(
                            snarl.edge_handle(cur_haplotype.first.back(), next_handle)) ==
                        incorrect_connections.end()) {
                        cerr
                            << "snarl starting at node " << source_id << " and ending at "
                            << sink_id
                            << " has a thread that incorrectly connects two nodes that "
                               "don't have any edge connecting them. These two nodes are "
                            << haploGraph.get_id(cur_haplotype.first.back()) << " and "
                            << haploGraph.get_id(next_handle)
                            << ". This thread connection will be ignored." << endl;
                        incorrect_connections.emplace(
                            snarl.edge_handle(cur_haplotype.first.back(), next_handle));

                        // todo: debug_statement
                        cerr << "next handle(s) of handle "
                             << snarl.get_id(cur_haplotype.first.back())
                             << " according to snarl:" << endl;
                        snarl.follow_edges(cur_haplotype.first.back(), false,
                                           [&](const handle_t handle) {
                                               cerr << "\t" << snarl.get_id(handle);
                                           });
                        cerr << endl;
                    }
                    continue;
                }
                // copy over the vector<handle_t> of cur_haplotype:
                vector<handle_t> next_handle_vec(cur_haplotype.first);

                // add the new handle to the vec:
                next_handle_vec.push_back(next_handle);

                // if new_handle is the sink, put in haplotypes_from_source_to_sink
                if (haploGraph.get_id(next_handle) == sink_id) {
                    haplotypes_from_source_to_sink.push_back(next_handle_vec);
                } else // keep extending the haplotype!
                {
                    pair<vector<handle_t>, gbwt::SearchState> next_haplotype =
                        make_pair(next_handle_vec, next_search);
                    haplotype_queue.push_back(next_haplotype);
                }
                // next_handle will be touched.
                touched_handles.emplace(next_handle);
            }
        }
        // if next_searches is empty, the path has ended but not reached sink.
        else if (next_searches.empty()) {
            // We have reached the end of the path, but it doesn't reach the sink.
            // we need to add cur_haplotype to other_haplotypes.
            other_haplotypes.push_back(cur_haplotype.first);

        }
        // if next_handle is the sink, put in haplotypes_from_source_to_sink
        else if (haploGraph.get_id(
                     haploGraph.node_to_handle(next_searches.back().node)) == sink_id) {
            // Then we need to add cur_haplotype + next_search to
            // haplotypes_from_source_to_sink.
            handle_t next_handle = haploGraph.node_to_handle(next_searches.back().node);
            cur_haplotype.first.push_back(next_handle);
            haplotypes_from_source_to_sink.push_back(cur_haplotype.first);

            // touched next_search's handle
            touched_handles.emplace(next_handle);
        }
        // else, there is just one next_search, and it's not the end of the path.
        // just extend the search by adding (cur_haplotype + next_search to
        // haplotype_queue.
        else {
            // get the next_handle from the one next_search.
            handle_t next_handle = haploGraph.node_to_handle(next_searches.back().node);

            // modify cur_haplotype with next_handle and next_search.
            cur_haplotype.first.push_back(next_handle);
            cur_haplotype.second =
                next_searches.back(); // there's only one next_search in next_searches.

            // put cur_haplotype back in haplotype_queue.
            haplotype_queue.push_back(cur_haplotype);
            touched_handles.emplace(next_handle);
        }
    }

    // Find any haplotypes starting from handles not starting at the source, but which
    // still start somewhere inside the snarl.
    vector<vector<handle_t>> haplotypes_not_starting_at_source =
        find_haplotypes_not_at_source(haploGraph, touched_handles, sink_id);

    // move haplotypes_not_starting_at_source into other_haplotypes:
    other_haplotypes.reserve(other_haplotypes.size() +
                             haplotypes_not_starting_at_source.size());
    move(haplotypes_not_starting_at_source.begin(),
         haplotypes_not_starting_at_source.end(), back_inserter(other_haplotypes));

    return tuple<vector<vector<handle_t>>, vector<vector<handle_t>>,
                 unordered_set<handle_t>>{haplotypes_from_source_to_sink,
                                          other_haplotypes, touched_handles};
}

// Used to complete the traversal of a snarl along its haplotype threads, when there are
// handles connected to the snarl by
//      threads that start after the source handle. (Threads that merely end before the
//      sink handle are addressed in extract_gbwt_haplotypes).
// Arguments:
//      haploGraph: the GBWTgraph containing the haplotype threads.
//      touched_handles: any handles found in the snarl so far.
//      sink_id: the id of the final handle in the snarl.
// Returns:
//      a vector of haplotypes in vector<handle_t> format that start in the middle of the
//      snarl.
vector<vector<handle_t>>
find_haplotypes_not_at_source(const gbwtgraph::GBWTGraph &haploGraph,
                              unordered_set<handle_t> &touched_handles,
                              const id_t &sink_id) {
    // cerr << "find_haplotypes_not_at_source" << endl;

    /// Search every handle in touched handles for haplotypes starting at that point.
    // Any new haplotypes will be added to haplotype_queue.
    vector<pair<vector<handle_t>, gbwt::SearchState>> haplotype_queue;

    // Fully extended haplotypes (or haplotypes extended to the snarl's sink)
    // will be added to finished_haplotypes.
    vector<vector<handle_t>> finished_haplotypes;

    // In addition, we need to put the new handle into to_search, because a path may have
    // started on the new handle (which means we need to start a searchstate there.)
    unordered_set<handle_t> to_search;

    // We don't need to ever check the sink handle, since paths from the sink handle
    // extend beyond snarl.
    handle_t sink_handle = haploGraph.get_handle(sink_id);
    // touched_handles.erase(sink_handle);

    // Nested function for making a new_search. Identifies threads starting at a given
    // handle and
    //      either adds them as a full haplotype (if the haplotype is one handle long) or
    //      makes a new entry to haplotype_queue.
    auto make_new_search = [&](handle_t handle) {
        // Are there any new threads starting at this handle?
        gbwt::SearchState new_search =
            haploGraph.index->prefix(haploGraph.handle_to_node(handle));
        if (!new_search.empty()) {
            // Then add them to haplotype_queue.
            haploGraph.follow_paths(
                new_search, [&](const gbwt::SearchState &next_search) -> bool {
                    handle_t next_handle = haploGraph.node_to_handle(next_search.node);

                    /// check to make sure that the thread isn't already finished:
                    // if next_handle is the sink, or if this thread is only one handle
                    // long, then there isn't any useful string to extract from this.
                    if (next_handle != sink_handle ||
                        next_search == gbwt::SearchState()) {
                        // establish a new thread to walk along.
                        vector<handle_t> new_path;
                        new_path.push_back(handle);
                        new_path.push_back(next_handle);

                        pair<vector<handle_t>, gbwt::SearchState> mypair =
                            make_pair(new_path, next_search);

                        // add the new path to haplotype_queue to be extended.
                        haplotype_queue.push_back(make_pair(new_path, next_search));

                        // if next_handle hasn't been checked for starting threads, add to
                        // to_search.
                        if (touched_handles.find(next_handle) == touched_handles.end()) {
                            to_search.emplace(next_handle);
                        }
                    }
                    return true;
                });
        }
    };

    /// Extend any paths in haplotype_queue, and add any newly found handles to to_search.
    /// Then, check to see if there are any new threads on handles in to_search.
    /// Extend those threads, and add any newly found handles to to_search,
    /// then search for threads again in to_search again... repeat until to_search remains
    /// emptied of new handles.

    // for tracking whether the haplotype thread is still extending:
    bool still_extending;
    while (!to_search.empty() || !haplotype_queue.empty()) {
        while (!haplotype_queue.empty()) {
            // get a haplotype to extend out of haplotype_queue - a tuple of
            // (handles_traversed_so_far, last_touched_SearchState)
            pair<vector<handle_t>, gbwt::SearchState> cur_haplotype =
                haplotype_queue.back();
            haplotype_queue.pop_back();

            // get all the subsequent search_states that immediately follow the
            // searchstate from cur_haplotype.
            vector<gbwt::SearchState> next_searches;
            haploGraph.follow_paths(cur_haplotype.second,
                                    [&](const gbwt::SearchState &next_search) -> bool {
                                        next_searches.push_back(next_search);
                                        return true;
                                    });

            for (gbwt::SearchState next_search : next_searches) {
                handle_t next_handle = haploGraph.node_to_handle(next_search.node);

                // if next_search is empty, then we've fallen off the thread,
                // and cur_haplotype can be placed in finished_haplotypes as is for this
                // thread.
                if (next_search == gbwt::SearchState()) {
                    finished_haplotypes.push_back(cur_haplotype.first);
                }

                // if next_search is on the sink_handle,
                // then cur_haplotype.first + next_search goes to finished_haplotypes.
                else if (haploGraph.get_id(next_handle) == sink_id) {

                    // copy over the vector<handle_t> of cur_haplotype:
                    vector<handle_t> next_handle_vec(cur_haplotype.first);
                    // add next_handle
                    next_handle_vec.push_back(next_handle);
                    // place in finished_haplotypes
                    finished_haplotypes.push_back(next_handle_vec);

                    // also, if next_handle hasn't been checked for new threads, add to
                    // to_search.
                    if (touched_handles.find(next_handle) != touched_handles.end()) {
                        to_search.emplace(next_handle);
                    }

                }
                // otherwise, just place an extended cur_haplotype in haplotype_queue.
                else {
                    // copy over cur_haplotype:
                    pair<vector<handle_t>, gbwt::SearchState> cur_haplotype_copy =
                        cur_haplotype;
                    // modify with next_handle/search
                    cur_haplotype_copy.first.push_back(next_handle);
                    cur_haplotype_copy.second = next_search;
                    // place back in haplotype_queue for further extension.
                    haplotype_queue.push_back(cur_haplotype_copy);

                    // also, if next_handle hasn't been checked for new threads, add to
                    // to_search.
                    if (touched_handles.find(next_handle) != touched_handles.end()) {
                        to_search.emplace(next_handle);
                    }
                }
            }
        }
        // Then, make more new_searches from the handles in to_search.
        for (handle_t handle : to_search) {
            make_new_search(handle); // will add to haplotype_queue if there's any
                                     // new_searches to be had.
        }
        to_search.clear();
    }
    return finished_haplotypes;
}

// Given a vector of haplotypes of format vector< handle_t >, returns a vector of
// haplotypes of
//      format string (which is the concatenated sequences in the handles).
// Arguments:
//      haploGraph: a gbwtgraph::GBWTGraph which contains the handles in vector< handle_t >
//      haplotypes. haplotypte_handle_vectors: a vector of haplotypes in vector< handle_t
//      > format.
// Returns: a vector of haplotypes of format string (which is the concatenated sequences
// in the handles).
vector<string> format_handle_haplotypes_to_strings(
    const gbwtgraph::GBWTGraph &haploGraph,
    const vector<vector<handle_t>> &haplotype_handle_vectors) {
    vector<string> haplotype_strings;
    for (vector<handle_t> haplotype_handles : haplotype_handle_vectors) {
        string hap;
        for (handle_t &handle : haplotype_handles) {
            hap += haploGraph.get_sequence(handle);
        }
        haplotype_strings.push_back(hap);
    }
    return haplotype_strings;
}

// TODO: eventually change to deal with haplotypes that start/end in middle of snarl.
// Aligns haplotypes to create a new graph using MSAConverter's seqan converter.
//      Assumes that each haplotype stretches from source to sink.
// Arguments:
//      source_to_sink_haplotypes: a vector of haplotypes in string format (concat of
//      handle sequences).
// Returns:
//      VG object representing the newly realigned snarl.
VG align_source_to_sink_haplotypes(vector<string> source_to_sink_haplotypes) {
    // cerr << "align_source_to_sink_haplotypes" << endl;
    // cerr << "number of strings to align: " << source_to_sink_haplotypes.size() << endl;
    // TODO: make the following comment true, so that I can normalize haplotypes that
    // TODO:    aren't source_to_sink by adding a similar special character to strings in
    // TODO:    the middle of the snarl.
    // modify source_to_sink_haplotypes to replace the leading and
    // trailing character with a special character. This ensures that the leading char of
    // the haplotype becomes the first character in the newly aligned snarl's source - it
    // maintains the context of the snarl.

    // store the source/sink chars for later reattachment to source and sink.
    string source_char(1, source_to_sink_haplotypes.back().front());
    string sink_char(1, source_to_sink_haplotypes.back().back());

    // for (string &hap : source_to_sink_haplotypes) {
    //     hap.replace(0, 1, "X");
    //     hap.replace(hap.size() - 1, 1, "X");
    // }

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

    seqan::resize(rows(align), source_to_sink_haplotypes.size());
    for (int i = 0; i < source_to_sink_haplotypes.size(); ++i) {
        assignSource(row(align, i), source_to_sink_haplotypes[i].c_str());
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
        cerr << "ROW_STRING: " << row_string << endl;
        // edit the row so that the proper source and sink chars are added to the
        // haplotype instead of the special characters added to ensure correct alignment
        // of source and sink.
        row_string.replace(0, 1, source_char);
        row_string.replace(row_string.size() - 1, 1, sink_char);
        row_strings.push_back(row_string);
    }

    stringstream ss;
    for (string seq : row_strings) {
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

/** For each handle in a given graph, divides any handles greater than max_size into parts
 * that are equal to or less than the size of max_size.
 *
 * @param  {MutableHandleGraph} graph : the graph in which we want to force a maximum
 * handle size for all handles.
 * @param  {size_t} max_size          : the maximum size we want a handle to be.
 */
void force_maximum_handle_size(MutableHandleGraph &graph, const size_t &max_size) {
    // forcing each handle in the graph to have a maximum sequence length of max_size:
    graph.for_each_handle([&](handle_t handle) {
        // all the positions we want to make in the handle are in offsets.
        vector<size_t> offsets;

        size_t sequence_len = graph.get_sequence(handle).size();
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
        graph.divide_handle(handle, offsets);
    });
}

// Finds all embedded paths that either start or end in a snarl (or both) defined by
// source_id, sink_id.
//      returns a vector of the embedded paths, where each entry in the vector is defined
//      by the pair of step_handles closest to the beginning and end of the path. If the
//      path is fully contained within the snarl, these step_handles will the be the
//      leftmost and rightmost handles in the path.
// Arguments:
//      graph: a pathhandlegraph containing the snarl with embedded paths.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns:
//      a vector containing all the embedded paths in the snarl, in pair< step_handle_t,
//      step_handle_t > > format. Pair.first is the first step in the path's range of
//      interest, and pair.second is the step *after* the last step in the path's range of
//      interest (can be the null step at end of path).
vector<pair<step_handle_t, step_handle_t>>
extract_embedded_paths_in_snarl(const PathHandleGraph &graph, const id_t &source_id,
                                const id_t &sink_id) {
    // cerr << "extract_embedded_paths_in_snarl" << endl;
    // cerr << "source id: " << source_id << endl;
    // cerr << "source id contains what paths?: " << endl;
    // for (auto step : graph.steps_of_handle(graph.get_handle(source_id))) {
    //     cerr << "\t" << graph.get_path_name(graph.get_path_handle_of_step(step)) <<
    //     endl;
    // }
    // cerr << "neighbors of 71104? (should include 71097):" << endl;
    // handle_t test_handle = graph.get_handle(71104);
    // graph.follow_edges(test_handle, true, [&](const handle_t &handle) {
    //     cerr << graph.get_id(handle) << endl;
    // });
    // cerr << "can I still access source handle?"
    //      << graph.get_sequence(graph.get_handle(source_id)) << endl;

    // get the snarl subgraph of the PathHandleGraph, in order to ensure that we don't
    // extend the path to a point beyond the source or sink.
    SubHandleGraph snarl = extract_subgraph(graph, source_id, sink_id);
    // key is path_handle, value is a step in that path from which to extend.
    unordered_map<path_handle_t, step_handle_t> paths_found;

    // look for handles with paths we haven't touched yet.
    snarl.for_each_handle([&](const handle_t &handle) {
        vector<step_handle_t> steps = graph.steps_of_handle(handle);
        // do any of these steps belong to a path not in paths_found?
        for (step_handle_t &step : steps) {
            path_handle_t path = graph.get_path_handle_of_step(step);
            // If it's a step along a new path, save the first step to that path we find.
            // In addtion, if there are multiple steps found in the path, (The avoidance
            // of source and sink here is to ensure that we can properly check to see if
            // we've reached the end of an embedded path walking in any arbitrary
            // direction (i.e. source towards sink or sink towards source).
            if (paths_found.find(path) == paths_found.end() ||
                graph.get_id(graph.get_handle_of_step(paths_found[path])) == source_id ||
                graph.get_id(graph.get_handle_of_step(paths_found[path])) == sink_id) {
                // then we need to mark it as found and save the step.
                paths_found[path] = step;
            }
        }
    });

    // todo: debug_statement
    // cerr << "################looking for new paths################" << endl;
    // for (auto path : paths_found) {
    //     cerr << graph.get_path_name(path.first) << " "
    //          << graph.get_id(graph.get_handle_of_step(path.second)) << endl;
    // }

    /// for each step_handle_t corresponding to a unique path, we want to get the steps
    /// closest to both the end and beginning step that still remains in the snarl.
    // TODO: Note copy paste of code here. In python I'd do "for fxn in [fxn1, fxn2]:",
    // TODO      so that I could iterate over the fxn. That sounds template-messy in C++
    // tho'. Should I?
    vector<pair<step_handle_t, step_handle_t>> paths_in_snarl;
    for (auto &it : paths_found) {
        step_handle_t step = it.second;
        // path_in_snarl describes the start and end steps in the path,
        // as constrained by the snarl.
        pair<step_handle_t, step_handle_t> path_in_snarl;

        // Look for the step closest to the beginning of the path, as constrained by the
        // snarl.
        step_handle_t begin_in_snarl_step = step;
        id_t begin_in_snarl_id =
            graph.get_id(graph.get_handle_of_step(begin_in_snarl_step));

        while ((begin_in_snarl_id != source_id) &&
               graph.has_previous_step(begin_in_snarl_step)) {
            begin_in_snarl_step = graph.get_previous_step(begin_in_snarl_step);
            begin_in_snarl_id =
                graph.get_id(graph.get_handle_of_step(begin_in_snarl_step));
        }
        path_in_snarl.first = begin_in_snarl_step;

        // Look for the step closest to the end of the path, as constrained by the snarl.
        step_handle_t end_in_snarl_step = step;
        id_t end_in_snarl_id = graph.get_id(graph.get_handle_of_step(end_in_snarl_step));

        // while (end_in_snarl_id != source_id and end_in_snarl_id != sink_id and
        //        graph.has_next_step(end_in_snarl_step)) {
        while (end_in_snarl_id != sink_id and graph.has_next_step(end_in_snarl_step)) {
            end_in_snarl_step = graph.get_next_step(end_in_snarl_step);
            end_in_snarl_id = graph.get_id(graph.get_handle_of_step(end_in_snarl_step));
        }
        // Note: when adding the end step, path notation convention requires that we add
        // the null step at the end of the path (or the next arbitrary step, in the case
        // of a path that extends beyond our snarl.)
        // TODO: do we want the next arbitrary step in that latter case?
        path_in_snarl.second = graph.get_next_step(end_in_snarl_step);

        paths_in_snarl.push_back(path_in_snarl);
    }

    return paths_in_snarl;
}

// TODO: change the arguments to handles, which contain orientation within themselves.
// Given a start and end node id, construct an extract subgraph between the two nodes
// (inclusive). Arguments:
//      graph: a pathhandlegraph containing the snarl with embedded paths.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns:
//      a SubHandleGraph containing only the handles in graph that are between start_id
//      and sink_id.
SubHandleGraph extract_subgraph(const HandleGraph &graph, const id_t &start_id,
                                const id_t &sink_id) {
    // cerr << "extract_subgraph" << endl;
    /// make a subgraph containing only nodes of interest. (e.g. a snarl)
    // make empty subgraph
    SubHandleGraph subgraph = SubHandleGraph(&graph);

    unordered_set<id_t> visited;  // to avoid counting the same node twice.
    unordered_set<id_t> to_visit; // nodes found that belong in the subgraph.

    // TODO: how to ensure that "to the right" of start_handle is the correct direction?
    // initialize with start_handle (because we move only to the right of start_handle):
    handle_t start_handle = graph.get_handle(start_id);
    subgraph.add_handle(start_handle);
    visited.insert(graph.get_id(start_handle));

    // look only to the right of start_handle
    graph.follow_edges(start_handle, false, [&](const handle_t &handle) {
        // mark the nodes to come as to_visit
        if (visited.find(graph.get_id(handle)) == visited.end()) {
            to_visit.insert(graph.get_id(handle));
        }
    });

    /// explore the rest of the snarl:
    while (to_visit.size() != 0) {
        // remove cur_handle from to_visit
        unordered_set<id_t>::iterator cur_index = to_visit.begin();
        handle_t cur_handle = graph.get_handle(*cur_index);

        to_visit.erase(cur_index);

        /// visit cur_handle
        visited.insert(graph.get_id(cur_handle));

        subgraph.add_handle(cur_handle);

        if (graph.get_id(cur_handle) != sink_id) { // don't iterate past end node!
            // look for all nodes connected to cur_handle that need to be added
            // looking to the left,
            graph.follow_edges(cur_handle, true, [&](const handle_t &handle) {
                // mark the nodes to come as to_visit
                if (visited.find(graph.get_id(handle)) == visited.end()) {
                    to_visit.insert(graph.get_id(handle));
                }
            });
            // looking to the right,
            graph.follow_edges(cur_handle, false, [&](const handle_t &handle) {
                // mark the nodes to come as to_visit
                if (visited.find(graph.get_id(handle)) == visited.end()) {
                    to_visit.insert(graph.get_id(handle));
                }
            });
        }
    }
    return subgraph;
}

// Integrates the snarl into the graph, replacing the snarl occupying the space between
// source_id and sink_id.
//      In the process, transfers any embedded paths traversing the old snarl into the new
//      snarl.
// Arguments:
//      graph: the graph in which we want to insert the snarl.
//      to_insert_snarl: a *separate* handle_graph from graph, often generated from
//      MSAconverter. embedded_paths: a vector of paths, where each is a pair.
//                        pair.first is the first step_handle of interest in the
//                        old_embedded_path, and pair.second is the step_handle *after*
//                        the last step_handle of interest in the old_embedded_path (can
//                        be the null step at the end of the path.)
//      source_id: the source of the old (to be replaced) snarl in graph
//      sink_id: the sink of the old (to be replaced) snarl in graph.
// Return: None.
// TODO: Note: How to ensure that step_handle_t's walk along the snarl in the same
// TODO:     orientation as we expect? i.e. that they don't move backward? I think
// TODO:     we want match_orientation to be = true, but this may cause problems
// TODO:     in some cases given the way we currently construct handles (fixed when we
// TODO:     create snarl-scanning interface).
// TODO:     It may also be that we *don't want match_orientation to be true,
// TODO:     if we're tracking a path that loops backward in the snarl. Hmm... Will think
// about this.
void integrate_snarl(MutablePathDeletableHandleGraph &graph,
                     const HandleGraph &to_insert_snarl,
                     const vector<pair<step_handle_t, step_handle_t>> embedded_paths,
                     const id_t &source_id, const id_t &sink_id) {
    // cerr << "integrate_snarl" << endl;

    // //todo: debug_statement
    // cerr << "handles in to_insert_snarl:" << endl;
    // to_insert_snarl.for_each_handle([&](const handle_t &handle) {
    //     cerr << to_insert_snarl.get_id(handle) << " "
    //          << to_insert_snarl.get_sequence(handle) << " \t";
    // });
    // cerr << endl;
    // Get old graph snarl
    SubHandleGraph old_snarl = extract_subgraph(graph, source_id, sink_id);

    // TODO: debug_statement: Check to make sure that newly made snarl has only one start
    // and end.
    // TODO:     (shouldn't be necessary once we've implemented alignment with
    // leading/trailing special chars.) Identify old and new snarl start and sink
    pair<vector<handle_t>, vector<handle_t>> to_insert_snarl_defining_handles =
        debug_get_sources_and_sinks(to_insert_snarl);

    if (to_insert_snarl_defining_handles.first.size() > 1 ||
        to_insert_snarl_defining_handles.second.size() > 1) {
        cerr << "ERROR: newly made snarl from a snarl starting at " << source_id
             << " has more than one start or end. # of starts: "
             << to_insert_snarl_defining_handles.first.size()
             << " # of ends: " << to_insert_snarl_defining_handles.second.size() << endl;
        return;
    }

    /// Replace start and end handles of old graph snarl with to_insert_snarl start and
    /// end, and delete rest of old graph snarl:

    // add to_insert_snarl into graph without directly attaching the snarl to the graph
    // (yet).
    vector<handle_t> to_insert_snarl_topo_order =
        algorithms::lazier_topological_order(&to_insert_snarl);

    // Construct a parallel new_snarl_topo_order to identify
    // paralogous nodes between to_insert_snarl and the new snarl inserted in graph.
    vector<handle_t> new_snarl_topo_order;

    // integrate the handles from to_insert_snarl into the graph, and keep track of their
    // identities by adding them to new_snarl_topo_order.
    for (handle_t to_insert_snarl_handle : to_insert_snarl_topo_order) {
        handle_t graph_handle =
            graph.create_handle(to_insert_snarl.get_sequence(to_insert_snarl_handle));
        new_snarl_topo_order.push_back(graph_handle);
    }

    // Connect the newly made handles in the graph together the way they were connected in
    // to_insert_snarl:
    for (int i = 0; i < to_insert_snarl_topo_order.size(); i++) {
        to_insert_snarl.follow_edges(
            to_insert_snarl_topo_order[i], false, [&](const handle_t &snarl_handle) {
                // get topo_index of nodes to be connected to graph start handle
                auto it = find(to_insert_snarl_topo_order.begin(),
                               to_insert_snarl_topo_order.end(), snarl_handle);
                int topo_index = it - to_insert_snarl_topo_order.begin();

                // connect graph start handle
                graph.create_edge(new_snarl_topo_order[i],
                                  new_snarl_topo_order[topo_index]);
            });
    }

    // save the source and sink values of new_snarl_topo_order, since topological order is
    // not necessarily preserved by move_path_to_snarl. Is temporary b/c we need to
    // replace the handles with ones with the right id_t label for source and sink later
    // on.
    id_t temp_snarl_source_id = graph.get_id(new_snarl_topo_order.front());
    id_t temp_snarl_sink_id = graph.get_id(new_snarl_topo_order.back());

    // Add the neighbors of the source and sink of the original snarl to the new_snarl's
    // source and sink.
    // source integration:
    graph.follow_edges(
        graph.get_handle(source_id), true, [&](const handle_t &prev_handle) {
            graph.create_edge(prev_handle, graph.get_handle(temp_snarl_source_id));
        });
    graph.follow_edges(
        graph.get_handle(sink_id), false, [&](const handle_t &next_handle) {
            graph.create_edge(graph.get_handle(temp_snarl_sink_id), next_handle);
        });

    // For each path of interest, move it onto the new_snarl.
    for (auto path : embedded_paths) {
        // //todo: debug_statement
        // cerr << "the new sink id: " << temp_snarl_sink_id << endl;
        move_path_to_snarl(graph, path, new_snarl_topo_order, temp_snarl_source_id,
                           temp_snarl_sink_id, source_id, sink_id);
    }

    // Destroy the old snarl.
    old_snarl.for_each_handle(
        [&](const handle_t &handle) { graph.destroy_handle(handle); });

    // Replace the source and sink handles with ones that have the original source/sink id
    // (for compatibility with future iterations on neighboring top-level snarls using the
    // same snarl manager. Couldn't replace it before b/c we needed the old handles to
    // move the paths.
    handle_t new_source_handle = graph.create_handle(
        graph.get_sequence(graph.get_handle(temp_snarl_source_id)), source_id);
    handle_t new_sink_handle =
        graph.create_handle(graph.get_sequence(new_snarl_topo_order.back()), sink_id);

    // move the source edges:
    // TODO: note the copy/paste. Ask if there's a better way to do this (I totally could
    // in Python!)
    graph.follow_edges(graph.get_handle(temp_snarl_source_id), true,
                       [&](const handle_t &prev_handle) {
                           graph.create_edge(prev_handle, new_source_handle);
                       });
    graph.follow_edges(graph.get_handle(temp_snarl_source_id), false,
                       [&](const handle_t &next_handle) {
                           graph.create_edge(new_source_handle, next_handle);
                       });

    // move the sink edges:
    graph.follow_edges(graph.get_handle(temp_snarl_sink_id), true,
                       [&](const handle_t &prev_handle) {
                           graph.create_edge(prev_handle, new_sink_handle);
                       });
    graph.follow_edges(graph.get_handle(temp_snarl_sink_id), false,
                       [&](const handle_t &next_handle) {
                           graph.create_edge(new_sink_handle, next_handle);
                       });

    // move the paths:
    graph.for_each_step_on_handle(
        graph.get_handle(temp_snarl_source_id), [&](step_handle_t step) {
            graph.rewrite_segment(step, graph.get_next_step(step),
                                  vector<handle_t>{new_source_handle});
        });
    graph.for_each_step_on_handle(
        graph.get_handle(temp_snarl_sink_id), [&](step_handle_t step) {
            graph.rewrite_segment(step, graph.get_next_step(step),
                                  vector<handle_t>{new_sink_handle});
        });

    // delete the previously created source and sink:
    for (handle_t handle :
         {graph.get_handle(temp_snarl_source_id), graph.get_handle(temp_snarl_sink_id)}) {

        graph.destroy_handle(handle);
    }
}

// Moves a path from its original location in the graph to a new snarl,
//      defined by a vector of interconnected handles.
//      NOTE: the handles in new_snarl_handles may not preserve topological order after
//      being passed to this method, if they were ordered before.
// Arguments: graph: the graph containing the old_embedded_path and the handles in
// new_snarl_topo_order
//            old_embedded_path: a pair, where
//                          pair.first is the first step_handle of interest in the
//                          old_embedded_path, and pair.second is the step_handle *after*
//                          the last step_handle of interest in the old_embedded_path (can
//                          be the null step at the end of the path.)
//            new_snarl_topo_order: all the handles in the new snarl, inside the graph.
// Return: None.
void move_path_to_snarl(MutablePathDeletableHandleGraph &graph,
                        const pair<step_handle_t, step_handle_t> &old_embedded_path,
                        vector<handle_t> &new_snarl_handles, id_t &new_source_id,
                        id_t &new_sink_id, const id_t &old_source_id,
                        const id_t &old_sink_id) {
    // cerr << "move_path_to_snarl" << endl;
    // //TODO: debug_statement:
    // cerr << "path name: "
    //      << graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first))
    //      << endl;
    // cerr << "source: " << new_source_id << " sink: " << new_sink_id << endl;
    // if (graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first)) ==
    //     "chr10") {
    //     cerr << "\t\tstart and end of old embedded path: "
    //          << graph.get_id(graph.get_handle_of_step(old_embedded_path.first))
    //          << "end id"
    //          << graph.get_id(graph.get_handle_of_step(old_embedded_path.second)) <<
    //          endl;
    // }
    // cerr << "#### handles in snarl (according to move_path_to_snarl): ####" << endl;
    // for (handle_t handle : new_snarl_handles) {
    //     cerr << "\t" << graph.get_id(handle) << " " << graph.get_sequence(handle);
    // }
    // cerr << endl << endl;
    // cerr << "~~~~~ Handles following each handle:" << endl;
    // for (handle_t handle : new_snarl_handles) {
    //     cerr << "neighbors of handle " << graph.get_id(handle) << " ("
    //     <<graph.get_sequence(handle) <<"):" << endl; graph.follow_edges(handle, false,
    //     [&] (const handle_t& next_handle){
    //         cerr << "\t" << graph.get_id(next_handle) << " " <<
    //         graph.get_sequence(next_handle) << endl;
    //     });
    // }

    // get the sequence associated with the path
    string path_seq;
    step_handle_t cur_step = old_embedded_path.first;

    // if the old path is touching either/both the source/sink, we want to make sure that
    // the newly moved path also touches those. Otherwise, any paths that extend beyond
    // the source or sink may be cut into pieces when the portion of the path overlapping
    // the snarl is moved to a region inside the snarl.
    bool touching_source =
        (graph.get_id(graph.get_handle_of_step(old_embedded_path.first)) ==
         old_source_id);
    bool touching_sink = (graph.get_id(graph.get_handle_of_step(graph.get_previous_step(
                              old_embedded_path.second))) == old_sink_id);

    // extract the path sequence of the embedded path:
    while (cur_step != old_embedded_path.second) {
        path_seq += graph.get_sequence(graph.get_handle_of_step(cur_step));
        cur_step = graph.get_next_step(cur_step);
    }

    // TODO: debug_statement:
    // cerr << "\t\tpath sequence length: " << path_seq.size() << endl;
    // cerr << "path sequence: " << path_seq << endl;

    // for the given path, find every good possible starting handle in the new_snarl
    //      format of pair is < possible_path_handle_vec,
    //      starting_index_in_the_first_handle, current_index_in_path_seq>
    // //todo: debug_statement
    // cerr << "checking handles as start of path-seq" << endl;
    vector<tuple<vector<handle_t>, int, int>> possible_paths;
    for (handle_t handle : new_snarl_handles) {
        string handle_seq = graph.get_sequence(handle);

        // starting index is where the path would begin in the handle,
        // since it could begin in the middle of the handle.
        vector<int> starting_indices =
            check_handle_as_start_of_path_seq(handle_seq, path_seq);

        // if there is a starting index,
        if (starting_indices.size() != 0) {
            for (int starting_index : starting_indices) {
                if ((handle_seq.size() - starting_index) >= path_seq.size() &&
                    source_and_sink_handles_map_properly(graph, new_source_id,
                                                         new_sink_id, touching_source,
                                                         touching_sink, handle, handle)) {
                    // if the entire path fits inside the current handle, and if any
                    // paths that touched source and sink in the old snarl would be
                    // touching source and sink in the new snarl, then we've already
                    // found the full mapping location of the path! Move the path, end
                    // the method.
                    vector<handle_t> new_path{handle};
                    graph.rewrite_segment(old_embedded_path.first,
                                          old_embedded_path.second, new_path);
                    // //todo: debug_statement
                    // cerr << "found a full mapping at " << graph.get_id(handle)
                    //      << " w/ seq " << graph.get_sequence(handle) << endl;
                    return;
                } else {
                    // this is a potential starting handle for the path. Add as a
                    // possible_path.
                    vector<handle_t> possible_path_handle_vec{handle};
                    possible_paths.push_back(
                        make_tuple(possible_path_handle_vec, starting_index,
                                   handle_seq.size() - starting_index));
                }
            }
        }
    }

    // //todo: debug_statement:
    // cerr << "done checking handles as start of path seq" << endl;

    // //TODO: debug_statement:
    // cerr << "possible paths so far: " << endl;
    // for (tuple<vector<handle_t>, int, int> path : possible_paths) {
    //     cerr << " possible start: ";
    //     for (handle_t handle : get<0>(path)) {
    //         cerr << graph.get_id(handle) << " ";
    //     }
    //     cerr << endl;
    // }

    // for every possible path, extend it to determine if it really is the path we're
    // looking for:
    while (!possible_paths.empty()) {
        // take a path off of possible_paths, which will be copied for every iteration
        // through graph.follow_edges, below:
        tuple<vector<handle_t>, int, int> possible_path_query = possible_paths.back();
        possible_paths.pop_back();

        // //TODO: debug_statement:
        // for (tuple<vector<handle_t>, int, int> path : possible_paths) {
        // cerr << "*\tpossible path query: ";
        // for (handle_t handle : get<0>(possible_path_query)) {
        //     cerr << graph.get_id(handle) << " " << graph.get_sequence(handle) << " ";
        // }
        // cerr << endl;
        // }

        // extend the path through all right-extending edges to see if any subsequent
        // paths still satisfy the requirements for being a possible_path:
        bool no_path = graph.follow_edges(
            get<0>(possible_path_query).back(), false, [&](const handle_t &next) {
                // //todo: debug_statement
                // cerr << "next handle id and seq: " << graph.get_id(next) << " "
                //      << graph.get_sequence(next) << endl;
                // make a copy to be extended for through each possible next handle in
                // follow edges.
                tuple<vector<handle_t>, int, int> possible_path = possible_path_query;

                // extract relevant information to make code more readable.
                string next_seq = graph.get_sequence(next);
                id_t next_id = graph.get_id(next);
                int &cur_index_in_path = get<2>(possible_path);
                if (cur_index_in_path <= path_seq.size() &&
                    (find(new_snarl_handles.cbegin(), new_snarl_handles.cend(), next) !=
                     new_snarl_handles.cend())) {
                    // if the next handle would be the ending handle for the path,
                    if (next_seq.size() >= (path_seq.size() - cur_index_in_path)) {
                        // cerr << "next handle would be the ending handle for the path"
                        // << endl; check to see if the sequence in the handle is suitable
                        // for ending the path:
                        int compare_length = path_seq.size() - cur_index_in_path;

                        // //todo: debug_statement
                        // cerr << "about to compare. compare val: "
                        //      << (next_seq.compare(0, compare_length, path_seq,
                        //                           cur_index_in_path, compare_length) ==
                        //                           0)
                        //      << " source_and_sink_handles_map "
                        //      << source_and_sink_handles_map_properly(
                        //             graph, new_source_id, new_sink_id, touching_source,
                        //             touching_sink, get<0>(possible_path).front(), next)
                        //      << endl;
                        if ((next_seq.compare(0, compare_length, path_seq,
                                              cur_index_in_path, compare_length) == 0) &&
                            source_and_sink_handles_map_properly(
                                graph, new_source_id, new_sink_id, touching_source,
                                touching_sink, get<0>(possible_path).front(), next)) {
                            // todo: debug_statement
                            // cerr << "compared." << endl;

                            // we've found the new path! Move path to the new sequence,
                            // and end the function.

                            if (compare_length < next_seq.size()) {
                                // If the path ends before the end of next_seq, then split
                                // the handle so that the path ends flush with the end of
                                // the first of the two split handles.

                                // divide the handle where the path ends;
                                pair<handle_t, handle_t> divided_next =
                                    graph.divide_handle(next, compare_length);
                                get<0>(possible_path).push_back(divided_next.first);

                                // Special case if next is the sink or the source, to
                                // preserve the reassignment of source and sink ids in
                                // integrate_snarl.
                                if (next_id == new_sink_id) {
                                    new_sink_id = graph.get_id(divided_next.second);
                                }

                                // TODO: NOTE: finding the old "next" handle is expensive.
                                // TODO:   Use different container?
                                auto it = find(new_snarl_handles.begin(),
                                               new_snarl_handles.end(), next);

                                // replace the old invalidated handle with one of the new
                                // ones
                                *it = divided_next.first;
                                // stick the other new handle on the end of
                                // new_snarl_handles.
                                new_snarl_handles.push_back(divided_next.second);

                            } else {
                                // otherwise, the end of the path already coincides with
                                // the end of the handle. In that case, just add it to the
                                // path.
                                get<0>(possible_path).push_back(next);
                            }
                            graph.rewrite_segment(old_embedded_path.first,
                                                  old_embedded_path.second,
                                                  get<0>(possible_path));
                            // //todo: debug_statement:
                            // cerr << "got a full path: ";
                            // for (handle_t handle : get<0>(possible_path)) {
                            //     cerr << graph.get_id(handle) << " ";
                            // }
                            // cerr << endl;

                            // we've already found the path. No need to keep looking for
                            // more paths.
                            return false;
                        }
                    }
                    // see if the next handle would be the continuation of the path, but
                    // not the end,
                    else {

                        // check to see if the sequence in the handle is suitable for
                        // extending the path:
                        int compare_length = next_seq.size();
                        // //todo: debug_statement
                        // cerr << "compare returned false" << endl;
                        // cerr << "compare in returned false: "
                        //      << " next_seq len " << next_seq.size() << " compare_length
                        //      "
                        //      << compare_length << " path_seq len " << path_seq.size()
                        //      << " cur_index_in_path " << cur_index_in_path << endl;
                        // cerr << "if statement eval: cur_index_in_path <=
                        // next_seq.size() "
                        //      << (cur_index_in_path <= next_seq.size())
                        //      << " next_seq.compare(0, compare_length, path_seq, "
                        //         "cur_index_in_path, compare_length) == 0) "
                        //      << (next_seq.compare(0, compare_length, path_seq,
                        //                           cur_index_in_path, compare_length) ==
                        //                           0)
                        //      << endl;
                        if (next_seq.compare(0, compare_length, path_seq,
                                             cur_index_in_path, compare_length) == 0) {
                            // cerr << "compared in return false" << endl;
                            // extend the path
                            get<0>(possible_path).push_back(next);

                            // update the current index in path_seq.
                            get<2>(possible_path) += next_seq.size();

                            // place back into possible_paths
                            possible_paths.push_back(possible_path);
                            // cerr << "extending the path!" << endl;
                        }
                    }
                }
                // continue to iterate through follow_edges.
                return true;
            });

        // //todo: debug_statement:
        // if
        // (graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first))
        // ==
        //     "_alt_19f9bc9ad2826f58f113965edf36bb93740df46d_0") {
        //     cerr << "mystery node 4214930: "
        //          << graph.get_sequence(graph.get_handle(4214930)) << endl;
        // }

        // if we've found a complete path in the above follow_edges, then we've
        // already moved the path, and we're done.
        if (!no_path) {
            return;
        }
    }
    // //todo: figure out how to do some better error message instead of cerr.
    // if we failed to find a path, show an error message.
    cerr << "##########################\nWarning! Didn't find a corresponding path of "
            "name "
         << graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first))
         << " from the old snarl at " << old_source_id
         << " in the newly aligned snarl. This snarl WILL be "
            "normalized, resulting in a probably incorrectly-constructed snarl."
            "\n##########################"
         << endl
         << endl;
    // throw graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first));
    // assert(true && "Warning! Didn't find a corresponding path of name " +
    //         graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first))
    //         + " from the old snarl in the newly aligned snarl.");
}

/** Used to help move_path_to_snarl map paths from an old snarl to its newly
 * normalized counterpart. In particular, ensures that any paths which touch the
 * source and/or sink of the old snarl still do so in the new snarl (which is
 * important to ensure that we don't break any paths partway through the snarl.)
 *
 * @param  {HandleGraph} graph         : the graph that contains the old and new snarl
 * nodes.
 * @param  {id_t} new_source_id        : the node id of the newly created source.
 * @param  {id_t} new_sink_id          : the node id of the newly created sink.
 * @param  {bool} touching_source      : true if the path is connected to the old
 * source.
 * @param  {bool} touching_sink        : true if the path is connected to the old
 * sink.
 * @param  {handle_t} potential_source : proposed source for the path in the new snarl.
 * @param  {handle_t} potential_sink   : proposed sink for the path in the new snarl.
 * @return {bool}                      : true if the path satisfies the requirement
 * that, if the original path covered the old source or sink, the new path also covers
 * the same respective nodes in the new snarl.
 */
bool source_and_sink_handles_map_properly(
    const HandleGraph &graph, const id_t &new_source_id, const id_t &new_sink_id,
    const bool &touching_source, const bool &touching_sink,
    const handle_t &potential_source, const handle_t &potential_sink) {

    bool path_map = false;
    // cerr << "touching source? " << touching_source << "touching_sink" << touching_sink
    //      << "source is source?" << (graph.get_id(potential_source) == new_source_id)
    //      << " sink is sink: " << (graph.get_id(potential_sink) == new_sink_id) << endl;
    if (touching_source && touching_sink) {
        path_map = ((graph.get_id(potential_source) == new_source_id) &&
                    (graph.get_id(potential_sink) == new_sink_id));
    } else if (touching_source) {
        path_map = (graph.get_id(potential_source) == new_source_id);
    } else if (touching_sink) {
        path_map = (graph.get_id(potential_sink) == new_sink_id);
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
vector<int> check_handle_as_start_of_path_seq(const string &handle_seq,
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

// Returns pair where pair.first is a vector of all sources of the given graph and
// path.second is all the sinks of the given graph. If graph is a subhandlegraph of a
// snarl, there should only be one source and sink each.
pair<vector<handle_t>, vector<handle_t>>
debug_get_sources_and_sinks(const HandleGraph &graph) {
    // cerr << "debug_get_source_and_sinks" << endl;
    vector<handle_t> sink;
    vector<handle_t> source;

    // identify sources and sinks
    graph.for_each_handle([&](const handle_t &handle) {
        bool is_source = true, is_sink = true;
        graph.follow_edges(handle, true, [&](const handle_t &prev) {
            is_source = false;
            return false;
        });
        graph.follow_edges(handle, false, [&](const handle_t &next) {
            is_sink = false;
            return false;
        });

        // base case for dynamic programming
        if (is_source) {
            source.push_back(handle);
        }
        if (is_sink) {
            sink.emplace_back(handle);
        }
    });
    return pair<vector<handle_t>, vector<handle_t>>(source, sink);
}

// Runs through the whole snarl and generates all possible strings representing walks
// from source to sink. Generates a combinatorial number of possible paths with splits
// in the snarl.
vector<string> debug_graph_to_strings(MutablePathDeletableHandleGraph &graph,
                                      id_t start_id, id_t sink_id) {
    // cerr << "debug_graph_to_strings" << endl;
    SubHandleGraph snarl = extract_subgraph(graph, start_id, sink_id);

    unordered_map<handle_t, vector<string>> sequences;
    vector<handle_t> sinks;
    unordered_map<handle_t, size_t> count;
    count.reserve(snarl.get_node_count());     // resize count to contain enough buckets
                                               // for size of snarl
    sequences.reserve(snarl.get_node_count()); // resize sequences to contain enough
                                               // buckets for size of snarl

    // identify sources and sinks //TODO: once we've established that this fxn works,
    // we can just use start_id and sink_id.
    snarl.for_each_handle([&](const handle_t &handle) {
        bool is_source = true, is_sink = true;
        snarl.follow_edges(handle, true, [&](const handle_t &prev) {
            is_source = false;
            return false;
        });
        snarl.follow_edges(handle, false, [&](const handle_t &next) {
            is_sink = false;
            return false;
        });

        // base case for dynamic programming
        if (is_source) {
            count[handle] = 1;
            sequences[handle].push_back(
                snarl.get_sequence(handle)); // TODO: presented in the handle's local
                                             // forward orientation. An issue?
        }
        if (is_sink) {
            sinks.emplace_back(handle);
        }
    });

    // count walks by dynamic programming
    bool overflowed = false;
    for (const handle_t &handle : algorithms::lazier_topological_order(&snarl)) {
        size_t count_here = count[handle];
        vector<string> seqs_here = sequences[handle];

        snarl.follow_edges(handle, false, [&](const handle_t &next) {
            size_t &count_next = count[next];
            string seq_next = snarl.get_sequence(next);

            if (numeric_limits<size_t>::max() - count_here < count_next) {
                overflowed = true;
            }

            else {
                count_next += count_here;
                for (string seq : seqs_here) {
                    sequences[next].push_back(seq + seq_next);
                }
            }
        });
        /// TODO: figure out how to deal with overflow.
        // if (overflowed) {
        //     return numeric_limits<size_t>::max();
        // }
    }

    // total up the walks at the sinks
    size_t total_count = 0;
    for (handle_t &sink : sinks) {
        total_count += count[sink];
    }

    // all the sequences at the sinks will be all the sequences in the snarl.
    vector<string> walks;
    for (handle_t &sink : sinks) {
        for (string seq : sequences[sink]) {
            walks.push_back(seq);
        }
    }

    return walks;
}

}
