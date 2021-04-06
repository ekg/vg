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
#include <seqan/align.h>
#include <seqan/graph_align.h>
#include <seqan/graph_msa.h>
// #include "../../deps/libhandlegraph/src/include/handlegraph/path_handle_graph.hpp"

namespace vg {

// TODO: allow for snarls that have haplotypes that begin or end in the middle of the
//      snarl
// Runs disambiguate_snarl on every top-level snarl in the graph, so long as the
//      snarl only contains haplotype threads that extend fully from source to sink.
// Arguments:
//      graph: the full-sized handlegraph that will undergo edits in a snarl.
//      haploGraph: the corresponding GBWTGraph of graph.
//      snarl_stream: the file stream from .snarl file corresponding to graph.
void disambiguate_top_level_snarls(MutablePathDeletableHandleGraph &graph,
                                   const GBWTGraph &haploGraph, ifstream &snarl_stream) {
    cerr << "disambiguate_top_level_snarls" << endl;
    SnarlManager *snarl_manager = new SnarlManager(snarl_stream);

    /** Use this code to count number of snarls in graph.
     *    int top_count = 0;
     *    for (const Snarl* snarl : snarl_manager->top_level_snarls()){
     *        top_count++;
     *    }
     *    cerr << "number of top_level snarls in graph: " << top_count << endl;
     *
     *    int general_count = 0;
     *    snarl_manager->for_each_snarl_preorder([&](const vg::Snarl * ignored){
     *        general_count++;
     *    });
     *    cerr << "number of total snarls in graph: " << general_count << endl;
     */

    int i = 0;
    vector<const Snarl *> snarl_roots = snarl_manager->top_level_snarls();
    for (auto roots : snarl_roots) {
        if (i == 2) {
            // TODO: debug_code:
            cerr << "return to root node ids, disambiguate snarl with..  " << endl;
            cerr << "root node ids: " << roots->start().node_id() << " "
                 << roots->end().node_id() << endl;
            disambiguate_snarl(graph, haploGraph, roots->start().node_id(),
                               roots->end().node_id());
        }


        // // TODO: debug_code:
        // cerr << "return to root node ids, disambiguate snarl with..  " << endl;
        // cerr << "root node ids: " << roots->start().node_id() << " "
        //         << roots->end().node_id() << endl;
        // disambiguate_snarl(graph, haploGraph, roots->start().node_id(),
        //                     roots->end().node_id());
        i += 1;
        cerr << endl << endl << "normalized " << i << " snarl(s)." << endl;
        if (i == 3) {
            break;
        }
    }

    delete snarl_manager;
}

// For a snarl in the given graph, with every edge covered by at least one haplotype
// thread in the GBWTGraph,
//      extract all sequences in the snarl corresponding to the haplotype threads and
//      re-align them with MSAConverter/seqan to form a new snarl. Embedded paths are
//      preserved; GBWT haplotypes in the snarl are not conserved.
// Arguments:
//      graph: the full-sized handlegraph that will undergo edits in a snarl.
//      haploGraph: the corresponding GBWTGraph of graph.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns: none.
// TODO: allow for snarls that have haplotypes that begin or end in the middle of the
// snarl.
void disambiguate_snarl(MutablePathDeletableHandleGraph &graph,
                        const GBWTGraph &haploGraph, const id_t &source_id,
                        const id_t &sink_id) {
    cerr << "disambiguate_snarl" << endl;

    // First, find all haplotypes encoded by the GBWT, in order to create the new snarl.
    // Return value is pair< haplotypes_that_stretch_from_source_to_sink,
    // haplotypes_that_end/start_prematurely >
    pair<vector<vector<handle_t>>, vector<vector<handle_t>>> haplotypes =
        extract_gbwt_haplotypes(haploGraph, source_id, sink_id);

    // TODO: this if statement removes snarls where a haplotype begins/ends in the middle
    // TODO:    of the snarl. Get rid of this once alignment issue is addressed!
    if (haplotypes.second.empty()) {
        // Convert the haplotypes from vector<handle_t> format to string format.
        vector<string> haplotypes_from_source_to_sink =
            format_handle_haplotypes_to_strings(haploGraph, haplotypes.first);
        // vector< string > other_haplotypes =
        // format_handle_haplotypes_to_strings(haploGraph, haplotypes.second);

        // Align the new snarl:
        // TODO: find better way to improve disamiguation of beginning/ending regions of
        // nodes
        // TODO:     than by adding leading/trailing AAA seq (essentially a special
        // character).
        cerr << "strings to be aligned: " << endl;
        for (string &hap : haplotypes_from_source_to_sink) {
            cerr << hap << endl;
            hap = "AAAAAAAA" + hap + "AAAAAAAA";
        }
        VG new_snarl = align_source_to_sink_haplotypes(haplotypes_from_source_to_sink);

        // Get the embedded paths in the snarl out of the graph, for the purposes of
        // moving them into the new snarl.
        vector<pair<step_handle_t, step_handle_t>> embedded_paths =
            extract_embedded_paths_in_snarl(graph, source_id, sink_id);

        cerr << "paths: " << endl;
        for (auto path : embedded_paths) {
            cerr << " path "
                 << graph.get_path_name(graph.get_path_handle_of_step(path.first))
                 << endl;
            for (auto step : {path.first, graph.get_previous_step(path.second)}) {
                cerr << "\t" << graph.get_id(graph.get_handle_of_step(step)) << " ";
            }
            cerr << endl;
        }

        // integrate the new_snarl into the graph, removing the old snarl as you go.
        integrate_snarl(graph, new_snarl, embedded_paths, source_id, sink_id);
        cerr << endl;

    } else {
        cerr << "found a snarl with haplotypes in the middle. Start: " << source_id
             << " sink is " << sink_id << endl;
    }
}

// TODO: test that it successfully extracts any haplotypes that start/end in the middle of
// TODO:    the snarl.
// For a snarl in a given GBWTGraph, extract all the haplotypes in the snarl. Haplotypes
// are represented
//      by vectors of handles, representing the chain of handles in a thread.
// Arguments:
//      haploGraph: the GBWTGraph containing the snarl.
//      source_id: the source of the snarl of interest.
//      sink_id: the sink of the snarl of interest.
// Returns:
//      a pair containting two sets of paths (each represented by a vector<handle_t>). The
//      first in the pair represents all paths reaching from source to sink in the snarl,
//      and the second representing all other paths in the snarl (e.g. any that don't
//      reach both source and sink in the graph.)
pair<vector<vector<handle_t>>, vector<vector<handle_t>>>
extract_gbwt_haplotypes(const GBWTGraph &haploGraph, const id_t &source_id,
                        const id_t &sink_id) {
    cerr << "extract_gbwt_haplotypes" << endl;

    // touched_handles contains all handles that have been touched by the
    // depth_first_search, for later use in other_haplotypes_to_strings, which identifies
    // paths that didn't stretch from source to sink in the snarl.
    unordered_set<handle_t> touched_handles;

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
    touched_handles.emplace(source_handle);

    // haplotypes contains all "finished" haplotypes - those that were either walked
    // to their conclusion, or until they reached the sink.
    vector<vector<handle_t>> haplotypes_from_source_to_sink;
    vector<vector<handle_t>> other_haplotypes;

    // for every partly-extracted thread, extend the thread until it either reaches
    // the sink of the snarl or the end of the thread.
    while (!haplotype_queue.empty()) {

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
        // if new_handle is the sink, put in haplotypes_from_source_to_sink
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

    return make_pair(haplotypes_from_source_to_sink, other_haplotypes);
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
find_haplotypes_not_at_source(const GBWTGraph &haploGraph,
                              unordered_set<handle_t> &touched_handles,
                              const id_t &sink_id) {
    cerr << "find_haplotypes_not_at_source" << endl;

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
    touched_handles.erase(sink_handle);

    // Nested function for making a new_search. Identifies threads starting at a given
    // handle and
    //      either adds them as a full haplotype (if the haplotype is one handle long) or
    //      makes a new entry to haplotype_queue.
    auto make_new_search = [&](handle_t handle) {
        // Are there any new threads starting at this handle?
        gbwt::SearchState new_search =
            haploGraph.index.prefix(haploGraph.handle_to_node(handle));
        if (!new_search.empty()) {
            // TODO: test_code code: are searchstates empty?
            cerr << "apparently new thread starts at node: " << haploGraph.get_id(handle)
                 << endl;
            cerr << "is the searchstate empty? " << new_search.empty()
                 << " size: " << new_search.size() << endl;
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
//      haploGraph: a GBWTGraph which contains the handles in vector< handle_t >
//      haplotypes. haplotypte_handle_vectors: a vector of haplotypes in vector< handle_t
//      > format.
// Returns: a vector of haplotypes of format string (which is the concatenated sequences
// in the handles).
vector<string> format_handle_haplotypes_to_strings(
    const GBWTGraph &haploGraph,
    const vector<vector<handle_t>> &haplotype_handle_vectors) {
    cerr << "format_handle_haplotypes_to_strings" << endl;
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
VG align_source_to_sink_haplotypes(const vector<string> &source_to_sink_haplotypes) {
    cerr << "align_source_to_sink_haplotypes" << endl;
    seqan::Align<seqan::DnaString> align; // create multiple_sequence_alignment object

    seqan::resize(rows(align), source_to_sink_haplotypes.size());
    for (int i = 0; i < source_to_sink_haplotypes.size(); ++i) {
        assignSource(row(align, i), source_to_sink_haplotypes[i].c_str());
    }

    globalMsaAlignment(align, seqan::SimpleScore(5, -3, -1, -3));

    stringstream ss;
    ss << align;
    MSAConverter myMSAConverter = MSAConverter();
    myMSAConverter.load_alignments(ss, "seqan");
    VG snarl = myMSAConverter.make_graph();
    snarl.clear_paths();

    // TODO: find better way to improve disamiguation of beginning/ending regions of nodes
    // TODO:     than by adding leading/trailing AAA seq (essentially a special
    // character).
    pair<vector<handle_t>, vector<handle_t>> source_and_sink =
        debug_get_sources_and_sinks(snarl);

    // Replace source with a handle that has the leading AAA seq removed.
    handle_t source = source_and_sink.first.back();
    string source_seq = snarl.get_sequence(source);
    id_t source_id = snarl.get_id(source);
    handle_t new_source = snarl.create_handle(source_seq.substr(8, source_seq.size()));
    snarl.follow_edges(source, false, [&](const handle_t &handle) {
        snarl.create_edge(new_source, handle);
    });
    snarl.destroy_handle(source);

    handle_t sink = source_and_sink.second.back();
    string sink_seq = snarl.get_sequence(sink);
    id_t sink_id = snarl.get_id(sink);
    handle_t new_sink = snarl.create_handle(sink_seq.substr(0, (sink_seq.size() - 8)));
    snarl.follow_edges(
        sink, true, [&](const handle_t &handle) { snarl.create_edge(handle, new_sink); });
    snarl.destroy_handle(sink);

    return snarl;
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
    cerr << "extract_embedded_paths_in_snarl" << endl;

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

        while ((begin_in_snarl_id != source_id) && (begin_in_snarl_id != sink_id) &&
               graph.has_previous_step(begin_in_snarl_step)) {
            begin_in_snarl_step = graph.get_previous_step(begin_in_snarl_step);
            begin_in_snarl_id =
                graph.get_id(graph.get_handle_of_step(begin_in_snarl_step));
        }
        path_in_snarl.first = begin_in_snarl_step;

        // Look for the step closest to the end of the path, as constrained by the snarl.
        step_handle_t end_in_snarl_step = step;
        id_t end_in_snarl_id = graph.get_id(graph.get_handle_of_step(end_in_snarl_step));

        while (end_in_snarl_id != source_id and end_in_snarl_id != sink_id and
               graph.has_next_step(end_in_snarl_step)) {
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
    cerr << "extract_subgraph" << endl;
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
    cerr << "integrate_snarl" << endl;
    // Get old graph snarl
    SubHandleGraph old_snarl = extract_subgraph(graph, source_id, sink_id);

    // TODO: test_code: Check to make sure that newly made snarl has only one start and
    // end.
    // TODO:     (shouldn't be necessary once we've implemented alignment with
    // leading/trailing special chars.) Identify old and new snarl start and sink
    pair<vector<handle_t>, vector<handle_t>> to_insert_snarl_defining_handles =
        debug_get_sources_and_sinks(to_insert_snarl);

    if (to_insert_snarl_defining_handles.first.size() > 1 ||
        to_insert_snarl_defining_handles.second.size() > 1) {
        cerr << "ERROR: newly made snarl with more than one start or end. # of starts: "
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
        move_path_to_snarl(graph, path, new_snarl_topo_order, temp_snarl_source_id,
                           temp_snarl_sink_id);
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
                        vector<handle_t> &new_snarl_handles, id_t &source_id,
                        id_t &sink_id) {
    cerr << endl << "move_path_to_snarl" << endl;

    cerr << "for path "
         << graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first))
         << endl;
    // get the sequence associated with the path
    string path_seq;
    step_handle_t cur_step = old_embedded_path.first;
    cerr << " old_embedded path looks like: "
         << graph.get_id(graph.get_handle_of_step(old_embedded_path.first)) << " "
         << graph.get_id(graph.get_handle_of_step(
                graph.get_previous_step(old_embedded_path.second)))
         << endl;

    cerr << "new snarl source and sink: " << source_id << " " << sink_id << endl;  

    while (cur_step != old_embedded_path.second) {
        path_seq += graph.get_sequence(graph.get_handle_of_step(cur_step));
        cur_step = graph.get_next_step(cur_step);
    }

    cerr << "pathseq: " << path_seq << endl;

    // for the given path, find every good possible starting handle in the new_snarl
    //      format of pair is < possible_path_handle_vec,
    //      starting_index_in_the_first_handle, current_index_in_path_seq>
    vector<tuple<vector<handle_t>, int, int>> possible_paths;
    for (handle_t handle : new_snarl_handles) {
        string handle_seq = graph.get_sequence(handle);
        // starting index is where the path would begin in the handle,
        // since it could begin in the middle of the handle.
        vector<int> starting_indices =
            check_handle_as_start_of_path_seq(handle_seq, path_seq);
        // TODO: debug_code: indices of start?
        // cerr << "indices of start in handle " << graph.get_id(handle) << " with sequence "
        //      << graph.get_sequence(handle) << "?" << endl;
        // for (auto start : starting_indices) {
        //     cerr << start << " " << graph.get_sequence(handle).substr(start) << endl;
        // }
        // cerr << endl;
        // cerr << "this what the seq looks like: " << endl;
        // for (auto start : starting_indices){
        //     cerr << start << " " << 
        // }
        // if there is a starting index,
        if (starting_indices.size() != 0) {
            // if the starting_indices implies that the starting handle entirely contains
            // the path_seq of interest:
            // cerr << " does the starting handle contain the pathseq? "
            //      << ((handle_seq.size() - starting_indices.back()) >= path_seq.size())
            //      << endl;
            // cerr << "handle_seq.size()" << handle_seq.size() << "starting_indices.back()"
            //      << starting_indices.back() << "path_seq.size()" << path_seq.size()
            //      << endl;

            if ((handle_seq.size() - starting_indices.back()) >= path_seq.size()) {
                cerr << " found a full path at node " << graph.get_id(handle) << " at " << starting_indices.back() << endl;
                // then we've already found the full mapping location of the path! Move
                // the path, end the method.
                vector<handle_t> new_path{handle};
                graph.rewrite_segment(old_embedded_path.first, old_embedded_path.second,
                                      new_path);
                return;
            } else {
                cerr << "adding possible path at node " << graph.get_id(handle) << " at " << endl;
                for (auto index : starting_indices){
                    cerr << index << " ";
                }
                cerr << endl;
                // add it as a possible_path.
                vector<handle_t> possible_path_handle_vec{handle};
                for (auto starting_index : starting_indices) {
                    possible_paths.push_back(
                        make_tuple(possible_path_handle_vec, starting_index,
                                   handle_seq.size() - starting_index));
                }
            }
        }
    }

    // for every possible path, extend it to determine if it really is the path we're
    // looking for:
    while (!possible_paths.empty()) {
        // take a path off of possible_paths, which will be copied for every iteration through graph.follow_edges, below:
        tuple<vector<handle_t>, int, int> possible_path_query = possible_paths.back();

        cerr << "possible paths looks like: " << endl;
        for (auto path: possible_paths){
            for (auto handle: get<0>(path)){
                cerr << graph.get_id(handle) << " ";
            }
            cerr << endl << "- - -" << endl;
        }
        cerr << endl;
        possible_paths.pop_back();

        // extend the path through all right-extending edges to see if any subsequent
        // paths still satisfy the requirements for being a possible_path:
        bool no_path = graph.follow_edges(
            get<0>(possible_path_query).back(), false, [&](const handle_t &next) {
                // make a copy to be extended for through each possible next handle in follow edges.
                tuple<vector<handle_t>, int, int> possible_path = possible_path_query;


                string next_seq = graph.get_sequence(next);
                id_t next_id = graph.get_id(next);
                cerr << "iterating through possible paths loop. id of next is: " << graph.get_id(next) << endl;
                cerr << "ALSO: possible paths looks like: ";
                for (auto handle: get<0>(possible_path)){
                    cerr << graph.get_id(handle) << " ";
                }
                cerr << endl;
                int &cur_index_in_path = get<2>(possible_path);


                // // if the next handle would be the ending handle for the path,
                // if (next_seq.size() >= (path_seq.size() - cur_index_in_path)) {
                //     // check to see if the sequence in the handle is suitable for ending
                //     // the path:
                //     int compare_length = path_seq.size() - cur_index_in_path;
                //     if (next_seq.compare(0, compare_length, path_seq, cur_index_in_path,
                //                          compare_length) == 0) {
                //         // we've found the new path! Move path to the new sequence, and
                //         // end the function.
                //         // TODO: move the path to the new vector of handles, splitting
                //         // start and end handles if need be. NOTE: if sink handle, we need
                //         // to ensure that the sink is properly placed at the end of the
                //         // new_snarl_topo_order (for future re-naming of sink id to be the
                //         // same as the original snarl).
                //         // if the path ends before the end of next_seq, then split the
                //         // handle so that the path ends flush with the end of the
                //         // first of the two split handles.
                //         if (compare_length < next_seq.size()) {
                //             pair<handle_t, handle_t> divided_next =
                //                 graph.divide_handle(next, compare_length);
                //             get<0>(possible_path).push_back(divided_next.first);
                //             find(new_snarl_handles.begin(), new_snarl_handles.end(), next);
                //             new_snarl_handles.push_back(divided_next.first);
                //         } else {
                //             get<0>(possible_path).push_back(next);
                //         }
                //         graph.rewrite_segment(old_embedded_path.first,
                //                               old_embedded_path.second,
                //                               get<0>(possible_path));

                //         // TODO: test_code: show when we find a path:
                //         cerr << "found a full path named "
                //              << graph.get_path_name(graph.get_path_handle_of_step(
                //                     old_embedded_path.first))
                //              << "! Here is the sequence of handles:" << endl;
                //         for (handle_t handle : get<0>(possible_path)) {
                //             cerr << graph.get_id(handle) << ": "
                //                  << graph.get_sequence(handle) << " " << endl;
                //         }
                //         return false;
                //     }
                // }
                
                // if the next handle would be the ending handle for the path,
                if (next_seq.size() >= (path_seq.size() - cur_index_in_path)) {
                    // check to see if the sequence in the handle is suitable for ending
                    // the path:
                    int compare_length = path_seq.size() - cur_index_in_path;
                    if (next_seq.compare(0, compare_length, path_seq, cur_index_in_path,
                                         compare_length) == 0) {
                        // we've found the new path! Move path to the new sequence, and
                        // end the function.

                        if (compare_length < next_seq.size()) {
                            // If the path ends before the end of next_seq, then split the
                            // handle so that the path ends flush with the end of the
                            // first of the two split handles.

                            // divide the handle where the path ends;
                            pair<handle_t, handle_t> divided_next =
                                graph.divide_handle(next, compare_length);
                            get<0>(possible_path).push_back(divided_next.first);
                            // Special case if next is the sink or the source, to preserve
                            // the reassignment of source and sink ids in integrate_snarl.
                            if (next_id = sink_id) {
                                sink_id = graph.get_id(divided_next.second);
                            }

                            // TODO: NOTE: finding the old "next" handle is expensive.
                            // TODO:   Use different container?
                            auto it = find(new_snarl_handles.begin(),
                                           new_snarl_handles.end(), next);

                            // replace the old invalidated handle with one of the new ones
                            *it = divided_next.first;
                            // stick the other new handle on the end of new_snarl_handles.
                            new_snarl_handles.push_back(divided_next.second);

                        } else {
                            // otherwise, the end of the path already coincides with the
                            // end of the handle. In that case, just add it to the path.
                            get<0>(possible_path).push_back(next);
                        }
                        graph.rewrite_segment(old_embedded_path.first,
                                              old_embedded_path.second,
                                              get<0>(possible_path));

                        // TODO: test_code: show when we find a path:
                        cerr << "found a full path named "
                             << graph.get_path_name(graph.get_path_handle_of_step(
                                    old_embedded_path.first))
                             << "! Here is the sequence of handles:" << endl;
                        for (handle_t handle : get<0>(possible_path)) {
                            cerr << graph.get_id(handle) << ": "
                                 << graph.get_sequence(handle) << " " << endl;
                        }
                        return false;
                    }
                }
                // see if the next handle would be the continuation of the path, but not
                // the end,
                else {
                    // check to see if the sequence in the handle is suitable for
                    // extending the path:
                    int compare_length = next_seq.size();
                    if (next_seq.compare(0, compare_length, path_seq, cur_index_in_path,
                                         compare_length) == 0) {
                        // extend the path
                        get<0>(possible_path).push_back(next);

                        cerr << "we've found an extension for a path starting at " << get<1>(possible_path) << ": ";
                        for (auto handle : get<0>(possible_path)){
                            cerr << graph.get_id(handle) << " ";
                        }
                        cerr << endl;

                        // update the current index in path_seq.
                        get<2>(possible_path) += next_seq.size();

                        // place back into possible_paths
                        possible_paths.push_back(possible_path);
                    }
                }
                // continue to iterate through follow_edges.
                return true;
            });

        // if we've found a complete path in the above follow_edges, then we've already
        // moved the path, and we're done.
        if (!no_path) {
            return;
        }
    }
    // if we failed to find a path, show an error message.
    // TODO: make this better! Throw an exception?
    cerr << "Warning! Didn't find a corresponding path of name "
         << graph.get_path_name(graph.get_path_handle_of_step(old_embedded_path.first))
         << " from the old snarl in the newly aligned snarl." << endl
         << endl;
    // cerr << "Here's the sequence of the path: " << path_seq << endl
    //      << "Here's the start and end node ids of the path: "
    //      << graph.get_id(graph.get_handle_of_step(old_embedded_path.first)) << " "
    //      << graph.get_id(graph.get_handle_of_step(old_embedded_path.second)) << endl
    //      << endl;
}

// Determines whether some subsequence in a handle satisfies the condition of being the
// beginning of a path.
//      If the path_seq is longer than the handle_seq, only checks subsequences that reach
//      from the beginning/middle of the handle_seq to the end. If path_seq is shorter
//      than handle_seq, checks for any substring of length path_seq within the
//      handle_seq, as well as substrings smaller than length path_seq that extend beyond
//      the current handle.
// Arguments:
//      handle_seq: the sequence in the handle we're trying to identify as a
//      start_of_path_seq. path_seq: the sequence in the path we're trying to find
//      starting points for in handle_seq
// Return: a vector of all potential starting index of the subsequence in the handle_seq.
vector<int> check_handle_as_start_of_path_seq(const string &handle_seq,
                                              const string &path_seq) {
    vector<int> possible_start_indices;
    cerr << "check_handle_as_start_of_path_seq" << endl;
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
    // if handle_seq.size > path_seq.size, look for any subsequence within handle_seq of
    // path_seq.size, as well as any subsequence smaller than path_seq reaching from
    // middle of handle_seq to the end of handle_seq.
    else {
        // first, search through all handle_seq for any comparable subsequence of
        // path_seq.size. Note: only differences between this for loop and above for loop
        // is that handle_start_i stops at (<= path_seq.size() - handle_seq.size()), and
        // subseq.size() = path_seq.size()
        for (int handle_start_i = 0;
             handle_start_i < (handle_seq.size() - path_seq.size()); handle_start_i++) {
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
        // second, search through the last few bases of handle_seq for the beginning of
        // path_seq. Note: nearly identical for loop to the one in "if (handle_seq.size()
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
    // Note: if we passed through the above check without returning anything, then there
    // isn't any satisfactory subsequence.
    return possible_start_indices;
}

// ------------------------------ DEBUG CODE BELOW:
// ------------------------------------------

// Returns pair where pair.first is a vector of all sources of the given graph and
// path.second is all the sinks of the given graph. If graph is a subhandlegraph of a
// snarl, there should only be one source and sink each.
pair<vector<handle_t>, vector<handle_t>>
debug_get_sources_and_sinks(const HandleGraph &graph) {
    cerr << "debug_get_source_and_sinks" << endl;
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

// Runs through the whole snarl and generates all possible strings representing walks from
// source to sink. Generates a combinatorial number of possible paths with splits in the
// snarl.
vector<string> debug_graph_to_strings(MutablePathDeletableHandleGraph &graph,
                                      id_t start_id, id_t sink_id) {
    cerr << "debug_graph_to_strings" << endl;
    SubHandleGraph snarl = extract_subgraph(graph, start_id, sink_id);

    unordered_map<handle_t, vector<string>> sequences;
    vector<handle_t> sinks;
    unordered_map<handle_t, size_t> count;
    count.reserve(snarl.get_node_count()); // resize count to contain enough buckets for
                                           // size of snarl
    sequences.reserve(snarl.get_node_count()); // resize sequences to contain enough
                                               // buckets for size of snarl

    // identify sources and sinks //TODO: once we've established that this fxn works, we
    // can just use start_id and sink_id.
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

} // namespace vg
