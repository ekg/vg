/// \file multipath_alignment_graph.cpp
///  
/// unit tests for the multipath mapper's MultipathAlignmentGraph

#include <iostream>
#include "json2pb.h"
#include "vg.pb.h"
#include "../multipath_alignment_graph.hpp"
#include "catch.hpp"

namespace vg {
namespace unittest {

/// We define a class to expose some protected members so we can look at them in the tests.
class TestableMultipathAlignmentGraph : public MultipathAlignmentGraph {
public:
    using MultipathAlignmentGraph::MultipathAlignmentGraph;
    using MultipathAlignmentGraph::path_nodes;
};

TEST_CASE( "MultipathAlignmentGraph::cut_at_forks cuts PathNodes at graph forks", "[multipath][mapping][multipathalignmentgraph]" ) {

    string graph_json = R"({
        "node": [
            {"id": 1, "sequence": "GAT"},
            {"id": 2, "sequence": "T"},
            {"id": 3, "sequence": "C"},
            {"id": 4, "sequence": "ACA"},
        ],
        "edge": [
            {"from": 1, "to": 2},
            {"from": 1, "to": 3},
            {"from": 2, "to": 4},
            {"from": 3, "to": 4}
        ]
    })";
    
    // Load the JSON
    Graph proto_graph;
    json2pb(proto_graph, graph_json.c_str(), graph_json.size());
    
    // Make it into a VG
    VG vg;
    vg.extend(proto_graph);
    
    // We need a fake read
    string read("GATTACA");
    
    // Pack it into an Alignment.
    // Note that we need to use the Alignment's copy for getting iterators for the MEMs.
    Alignment query;
    query.set_sequence(read);
    
    // Make an identity projection translation
    auto identity = MultipathAlignmentGraph::create_identity_projection_trans(vg);
    
    // Make up a fake MEM
    // GCSA range_type is just a pair of [start, end], so we can fake them.
    
    // This will actually own the MEMs
    vector<MaximalExactMatch> mems;
    
    // This will hold our MEMs and their start positions in the imaginary graph.
    // Note that this is also a memcluster_t
    vector<pair<const MaximalExactMatch*, pos_t>> mem_hits;
    
    // Make a MEM hit over 1, 2, 3
    mems.emplace_back(query.sequence().begin(), query.sequence().begin() + 7, make_pair(5, 5), 1);
    // Drop it on node 1 where it should sit
    mem_hits.emplace_back(&mems.back(), make_pos_t(1, false, 0));
    
    // Make the MultipathAlignmentGraph to test.
    // This will walk from the MEM starting point to find an actual path to spell the MEM.
    TestableMultipathAlignmentGraph mpg(vg, mem_hits, identity);
    
    // We should get only one PathNode
    REQUIRE(mpg.path_nodes.size() == 1);
    
    // Clear out the reachability edges
    mpg.clear_reachability_edges();
    
    // Cut up the single PathNode
    mpg.cut_at_forks(vg);
    
    // Now we should have three
    REQUIRE(mpg.path_nodes.size() == 3);
    
    // Check their toplological order and actual positions/paths.
    mpg.add_reachability_edges(vg, identity, MultipathAlignmentGraph::create_injection_trans(identity));
    vector<size_t> order;
    mpg.topological_sort(order);
    
    PathNode& first = mpg.path_nodes[order[0]];
    REQUIRE(first.begin == query.sequence().begin());
    REQUIRE(first.end == query.sequence().begin() + 3);
    REQUIRE(first.path.mapping_size() == 1);
    REQUIRE(first.path.mapping(0).position().node_id() == 1);
    
    PathNode& second = mpg.path_nodes[order[1]];
    REQUIRE(second.begin == query.sequence().begin() + 3);
    REQUIRE(second.end == query.sequence().begin() + 4);
    REQUIRE(second.path.mapping_size() == 1);
    REQUIRE(second.path.mapping(0).position().node_id() == 2);
    
    PathNode& third = mpg.path_nodes[order[2]];
    REQUIRE(third.begin == query.sequence().begin() + 4);
    REQUIRE(third.end == query.sequence().begin() + 7);
    REQUIRE(third.path.mapping_size() == 1);
    REQUIRE(third.path.mapping(0).position().node_id() == 4);
}

TEST_CASE( "MultipathAlignmentGraph::synthesize_anchors_by_search creates anchors", "[multipath][mapping][multipathalignmentgraph]" ) {

    string graph_json = R"({
        "node": [
            {"id": 1, "sequence": "GAT"},
            {"id": 2, "sequence": "T"},
            {"id": 3, "sequence": "C"},
            {"id": 4, "sequence": "ACA"},
        ],
        "edge": [
            {"from": 1, "to": 2},
            {"from": 1, "to": 3},
            {"from": 2, "to": 4},
            {"from": 3, "to": 4}
        ]
    })";
    
    // Load the JSON
    Graph proto_graph;
    json2pb(proto_graph, graph_json.c_str(), graph_json.size());
    
    // Make it into a VG
    VG vg;
    vg.extend(proto_graph);
    
    // We need a fake read
    string read("GATTACA");
    
    // Pack it into an Alignment.
    // Note that we need to use the Alignment's copy for getting iterators for the MEMs.
    Alignment query;
    query.set_sequence(read);
    
    // Make an identity projection translation
    auto identity = MultipathAlignmentGraph::create_identity_projection_trans(vg);
    
    // Make up a fake MEM
    // GCSA range_type is just a pair of [start, end], so we can fake them.
    
    // This will actually own the MEMs
    vector<MaximalExactMatch> mems;
    
    // This will hold our MEMs and their start positions in the imaginary graph.
    // Note that this is also a memcluster_t
    vector<pair<const MaximalExactMatch*, pos_t>> mem_hits;
    
    // Make a MEM hit over the middle of 1
    mems.emplace_back(query.sequence().begin() + 1, query.sequence().begin() + 2, make_pair(5, 5), 1);
    // Drop it on node 1 where it should sit
    mem_hits.emplace_back(&mems.back(), make_pos_t(1, false, 1));
    
    // Make the MultipathAlignmentGraph to test.
    // This will walk from the MEM starting point to find an actual path to spell the MEM.
    TestableMultipathAlignmentGraph mpg(vg, mem_hits, identity);
    
    // We should get only one PathNode
    REQUIRE(mpg.path_nodes.size() == 1);
    
    // Clear out the reachability edges
    mpg.clear_reachability_edges();
    
    // Cut up the PathNode if necessary
    mpg.cut_at_forks(vg);
    
    // Synthesize anchors
    mpg.synthesize_anchors_by_search(query, vg, 1);
    
    // We should get 4 new PathNodes: left of MEM on 1, right of MEM on 1, match on 2, and match on 4.
    // We shouldn't get anything overlapping the original PathNode because we only start the search from one place.
    REQUIRE(mpg.path_nodes.size() == 5);
}

TEST_CASE( "MultipathAlignmentGraph can second-guess anchors", "[multipath][mapping][multipathalignmentgraph]" ) {

    string graph_json = R"({
        "node": [
            {"id": 1, "sequence": "GA"},
            {"id": 2, "sequence": "TTTT"},
            {"id": 3, "sequence": "CT"},
            {"id": 4, "sequence": "ACA"},
        ],
        "edge": [
            {"from": 1, "to": 2},
            {"from": 1, "to": 3},
            {"from": 2, "to": 4},
            {"from": 3, "to": 4}
        ]
    })";
    
    // Load the JSON
    Graph proto_graph;
    json2pb(proto_graph, graph_json.c_str(), graph_json.size());
    
    // Make it into a VG
    VG vg;
    vg.extend(proto_graph);
    
    // We need a fake read
    string read("GATTACA");
    
    // Pack it into an Alignment.
    // Note that we need to use the Alignment's copy for getting iterators for the MEMs.
    Alignment query;
    query.set_sequence(read);
    
    // Make an Aligner to use for the actual aligning and the scores
    Aligner aligner;
    
    // Make an identity projection translation
    auto identity = MultipathAlignmentGraph::create_identity_projection_trans(vg);
    
    // Make up a fake MEM
    // GCSA range_type is just a pair of [start, end], so we can fake them.
    
    // This will actually own the MEMs
    vector<MaximalExactMatch> mems;
    
    // This will hold our MEMs and their start positions in the imaginary graph.
    // Note that this is also a memcluster_t
    vector<pair<const MaximalExactMatch*, pos_t>> mem_hits;
    
    // Make a MEM hit GA TT
    mems.emplace_back(query.sequence().begin(), query.sequence().begin() + 4, make_pair(5, 5), 1);
    // Drop it on node 1 where it should sit
    mem_hits.emplace_back(&mems.back(), make_pos_t(1, false, 0));
    
    // Make the MultipathAlignmentGraph to test.
    // This will walk from the MEM starting point to find an actual path to spell the MEM.
    TestableMultipathAlignmentGraph mpg(vg, mem_hits, identity);
    
    // We should get only one PathNode
    REQUIRE(mpg.path_nodes.size() == 1);
    
    // Clear out the reachability edges
    mpg.clear_reachability_edges();
    
    // Cut up the PathNode if necessary
    mpg.cut_at_forks(vg);
    
    // Synthesize anchors
    mpg.synthesize_anchors_by_search(query, vg, 1);
    
    // Restore reachability edges, hooking up the synthesized anchors.
    mpg.add_reachability_edges(vg, identity, MultipathAlignmentGraph::create_injection_trans(identity));
    
    mpg.to_dot(cerr);
    
    // Make the output MultipathAlignment
    MultipathAlignment out;
    
    // Make it align, with alignments per gap/tail
    mpg.align(query, vg, &aligner, true, 2, false, 5, out);
    
    cerr << pb2json(out) << endl;
    
    // Make sure to topologically sort the resulting alignment. TODO: Should
    // the MultipathAlignmentGraph guarantee this for us by construction?
    topologically_order_subpaths(out);
    
    // Make sure it worked at all
    REQUIRE(out.sequence() == read);
    REQUIRE(out.subpath_size() > 0);
    
    // Get the top optimal alignments
    vector<Alignment> opt = optimal_alignments(out, 100);
    
    // Convert to a set of vectors of visited node IDs
    set<vector<id_t>> got;
    for(size_t i = 0; i < opt.size(); i++) {
        auto& aln = opt[i];
        
        vector<id_t> ids;
        for (auto& mapping : aln.path().mapping()) {
            ids.push_back(mapping.position().node_id());
        }
        
        // Save each list of visited IDs for checking.
        got.insert(ids);
    }
    
    // Make sure we have the good alignment that only takes a SNP but which our
    // original anchors didn't allow.
    REQUIRE(got.count({1, 3, 4}));
    
    
    
}
    

TEST_CASE( "MultipathAlignmentGraph::align handles tails correctly", "[multipath][mapping][multipathalignmentgraph]" ) {

    string graph_json = R"({
        "node": [
            {"id": 1, "sequence": "GATT"},
            {"id": 2, "sequence": "A"},
            {"id": 3, "sequence": "G"},
            {"id": 4, "sequence": "C"},
            {"id": 5, "sequence": "A"},
            {"id": 6, "sequence": "G"},
            {"id": 7, "sequence": "A"}
        ],
        "edge": [
            {"from": 1, "to": 2},
            {"from": 1, "to": 3},
            {"from": 2, "to": 4},
            {"from": 3, "to": 4},
            {"from": 4, "to": 5},
            {"from": 4, "to": 6},
            {"from": 5, "to": 7},
            {"from": 6, "to": 7}
        ]
    })";
    
    // Load the JSON
    Graph proto_graph;
    json2pb(proto_graph, graph_json.c_str(), graph_json.size());
    
    // Make it into a VG
    VG vg;
    vg.extend(proto_graph);
    
    // Make snarls on it
    CactusSnarlFinder bubble_finder(vg);
    SnarlManager snarl_manager = bubble_finder.find_snarls(); 
    
    // We need a fake read
    string read("GATTACAA");
    
    // Pack it into an Alignment.
    // Note that we need to use the Alignment's copy for getting iterators for the MEMs.
    Alignment query;
    query.set_sequence(read);
    
    // Make an Aligner to use for the actual aligning and the scores
    Aligner aligner;
        
    // Make an identity projection translation
    auto identity = MultipathAlignmentGraph::create_identity_projection_trans(vg);
    
    // Make up a fake MEM
    // GCSA range_type is just a pair of [start, end], so we can fake them.
    
    // This will actually own the MEMs
    vector<MaximalExactMatch> mems;
    
    // This will hold our MEMs and their start positions in the imaginary graph.
    // Note that this is also a memcluster_t
    vector<pair<const MaximalExactMatch*, pos_t>> mem_hits;
    
    
    SECTION ("Works with right tail only") {
    
        // Make a MEM hit over all of node 1's sequence
        mems.emplace_back(query.sequence().begin(), query.sequence().begin() + 4, make_pair(5, 5), 1);
        // Drop it on node 1 where it should sit
        mem_hits.emplace_back(&mems.back(), make_pos_t(1, false, 0));
        
        // Make the MultipathAlignmentGraph to test
        MultipathAlignmentGraph mpg(vg, mem_hits, identity);
        
        // Make the output MultipathAlignment
        MultipathAlignment out;
        
        SECTION("Tries multiple traversals of snarls in tails") {
        
            // Generate 2 fake tail anchors
            mpg.synthesize_tail_anchors(query, vg, &aligner, 2, false);
            
            // Cut new anchors on snarls
            mpg.resect_snarls_from_paths(&snarl_manager, identity, 5);
            
            // Make it align, with alignments per gap/tail
            mpg.align(query, vg, &aligner, true, 2, false, 5, out);
            
            // Make sure to topologically sort the resulting alignment. TODO: Should
            // the MultipathAlignmentGraph guarantee this for us by construction?
            topologically_order_subpaths(out);
            
            // Make sure it worked at all
            REQUIRE(out.sequence() == read);
            REQUIRE(out.subpath_size() > 0);
            
            // Get the top optimal alignments
            vector<Alignment> opt = optimal_alignments(out, 100);
            
            // Convert to a set of vectors of visited node IDs
            set<vector<id_t>> got;
            for(size_t i = 0; i < opt.size(); i++) {
                auto& aln = opt[i];
                
                vector<id_t> ids;
                for (auto& mapping : aln.path().mapping()) {
                    ids.push_back(mapping.position().node_id());
                }
                
                // Save each list of visited IDs for checking.
                got.insert(ids);
            }
            
            // Make sure all combinations are there
            REQUIRE(got.count({1, 2, 4, 6, 7}));
            REQUIRE(got.count({1, 2, 4, 5, 7}));
            REQUIRE(got.count({1, 3, 4, 6, 7}));
            REQUIRE(got.count({1, 3, 4, 5, 7}));
            
        }
        
        SECTION("Handles tails when anchors for them are not generated") {
        
            // Make it align, with alignments per gap/tail
            mpg.align(query, vg, &aligner, true, 2, false, 5, out);
            
            // Make sure to topologically sort the resulting alignment. TODO: Should
            // the MultipathAlignmentGraph guarantee this for us by construction?
            topologically_order_subpaths(out);
            
            // Make sure it worked at all
            REQUIRE(out.sequence() == read);
            REQUIRE(out.subpath_size() > 0);
            
            // Get the top optimal alignments
            vector<Alignment> opt = optimal_alignments(out, 100);
            
            // Convert to a set of vectors of visited node IDs
            set<vector<id_t>> got;
            for(size_t i = 0; i < opt.size(); i++) {
                auto& aln = opt[i];
                
                vector<id_t> ids;
                for (auto& mapping : aln.path().mapping()) {
                    ids.push_back(mapping.position().node_id());
                }
                
                // Save each list of visited IDs for checking.
                got.insert(ids);
            }
            
            // Make sure the best alignment is there.
            REQUIRE(got.count({1, 2, 4, 5, 7}));
        
        }
        
    }
    
    
    SECTION ("Works with both tails at once") {
    
        // Make a MEM hit over all of node 4's sequence
        mems.emplace_back(query.sequence().begin() + 5, query.sequence().begin() + 6, make_pair(5, 5), 1);
        // Drop it on node 4 where it should sit
        mem_hits.emplace_back(&mems.back(), make_pos_t(4, false, 0));
        
        // Make the MultipathAlignmentGraph to test
        MultipathAlignmentGraph mpg(vg, mem_hits, identity);
        
        // Make the output MultipathAlignment
        MultipathAlignment out;
        
        
        SECTION("Tries multiple traversals of snarls in tails") {
        
            // Generate 2 fake tail anchors
            mpg.synthesize_tail_anchors(query, vg, &aligner, 2, false);
            
            // Cut new anchors on snarls
            mpg.resect_snarls_from_paths(&snarl_manager, identity, 5);
            
            // Make it align, with alignments per gap/tail
            mpg.align(query, vg, &aligner, true, 2, false, 5, out);
            
            // Make sure to topologically sort the resulting alignment. TODO: Should
            // the MultipathAlignmentGraph guarantee this for us by construction?
            topologically_order_subpaths(out);
            
            // Make sure it worked at all
            REQUIRE(out.sequence() == read);
            REQUIRE(out.subpath_size() > 0);
            
            // Get the top optimal alignments
            vector<Alignment> opt = optimal_alignments(out, 100);
            
            // Convert to a set of vectors of visited node IDs
            set<vector<id_t>> got;
            for(size_t i = 0; i < opt.size(); i++) {
                auto& aln = opt[i];
                
                vector<id_t> ids;
                for (auto& mapping : aln.path().mapping()) {
                    ids.push_back(mapping.position().node_id());
                }
                
                // Save each list of visited IDs for checking.
                got.insert(ids);
            }
            
            // Make sure all combinations are there
            REQUIRE(got.count({1, 2, 4, 6, 7}));
            REQUIRE(got.count({1, 2, 4, 5, 7}));
            REQUIRE(got.count({1, 3, 4, 6, 7}));
            REQUIRE(got.count({1, 3, 4, 5, 7}));
            
        }
        
        
        SECTION("Handles tails when anchors for them are not generated") {
            // Make it align, with alignments per gap/tail
            mpg.align(query, vg, &aligner, true, 2, false, 5, out);
            
            // Make sure to topologically sort the resulting alignment. TODO: Should
            // the MultipathAlignmentGraph guarantee this for us by construction?
            topologically_order_subpaths(out);
            
            // Make sure it worked at all
            REQUIRE(out.sequence() == read);
            REQUIRE(out.subpath_size() > 0);
            
            // Get the top optimal alignments
            vector<Alignment> opt = optimal_alignments(out, 100);
            
            // Convert to a set of vectors of visited node IDs
            set<vector<id_t>> got;
            for(size_t i = 0; i < opt.size(); i++) {
                auto& aln = opt[i];
                
                vector<id_t> ids;
                for (auto& mapping : aln.path().mapping()) {
                    ids.push_back(mapping.position().node_id());
                }
                
                // Save each list of visited IDs for checking.
                got.insert(ids);
            }
            
            // Make sure the best alignment is there.
            REQUIRE(got.count({1, 2, 4, 5, 7}));
        }
        
    }
    
    
    SECTION ("Works with left tail only") {
    
        // Make a MEM hit over all of node 7's sequence
        mems.emplace_back(query.sequence().begin() + 7, query.sequence().begin() + 8, make_pair(5, 5), 1);
        // Drop it on node 7 where it should sit
        mem_hits.emplace_back(&mems.back(), make_pos_t(7, false, 0));
        
        // Make the MultipathAlignmentGraph to test
        MultipathAlignmentGraph mpg(vg, mem_hits, identity);
        
        // Make the output MultipathAlignment
        MultipathAlignment out;
        
        SECTION("Tries multiple traversals of snarls in tails") {
        
            // Generate 2 fake tail anchors
            mpg.synthesize_tail_anchors(query, vg, &aligner, 2, false);
            
            // Cut new anchors on snarls
            mpg.resect_snarls_from_paths(&snarl_manager, identity, 5);
            
            // Make it align, with alignments per gap/tail
            mpg.align(query, vg, &aligner, true, 2, false, 5, out);
            
            // Make sure to topologically sort the resulting alignment. TODO: Should
            // the MultipathAlignmentGraph guarantee this for us by construction?
            topologically_order_subpaths(out);
            
            // Make sure it worked at all
            REQUIRE(out.sequence() == read);
            REQUIRE(out.subpath_size() > 0);
            
            // Get the top optimal alignments
            vector<Alignment> opt = optimal_alignments(out, 100);
            
            // Convert to a set of vectors of visited node IDs
            set<vector<id_t>> got;
            for(size_t i = 0; i < opt.size(); i++) {
                auto& aln = opt[i];
                
                vector<id_t> ids;
                for (auto& mapping : aln.path().mapping()) {
                    ids.push_back(mapping.position().node_id());
                }
                
                // Save each list of visited IDs for checking.
                got.insert(ids);
            }
            
            // Make sure all combinations are there
            REQUIRE(got.count({1, 2, 4, 6, 7}));
            REQUIRE(got.count({1, 2, 4, 5, 7}));
            REQUIRE(got.count({1, 3, 4, 6, 7}));
            REQUIRE(got.count({1, 3, 4, 5, 7}));
            
        }
        
        SECTION("Handles tails when anchors for them are not generated") {
            // Make it align, with alignments per gap/tail
            mpg.align(query, vg, &aligner, true, 2, false, 5, out);
            
            // Make sure to topologically sort the resulting alignment. TODO: Should
            // the MultipathAlignmentGraph guarantee this for us by construction?
            topologically_order_subpaths(out);
            
            // Make sure it worked at all
            REQUIRE(out.sequence() == read);
            REQUIRE(out.subpath_size() > 0);
            
            // Get the top optimal alignments
            vector<Alignment> opt = optimal_alignments(out, 100);
            
            // Convert to a set of vectors of visited node IDs
            set<vector<id_t>> got;
            for(size_t i = 0; i < opt.size(); i++) {
                auto& aln = opt[i];
                
                vector<id_t> ids;
                for (auto& mapping : aln.path().mapping()) {
                    ids.push_back(mapping.position().node_id());
                }
                
                // Save each list of visited IDs for checking.
                got.insert(ids);
            }
            
            // Make sure the best alignment is there.
            REQUIRE(got.count({1, 2, 4, 5, 7})); 
        }
        
    }
    
        
}

}

}
