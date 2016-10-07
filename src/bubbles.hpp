#ifndef BUBBLES_H

#define BUBBLES_H

#include <vector>
#include <map>

#include "types.hpp"
#include "utility.hpp"
#include "nodeside.hpp"

#include "DetectSuperBubble.hpp"
extern "C" {
    typedef struct _stCactusGraph stCactusGraph;
    typedef struct _stCactusNode stCactusNode;;
}

using namespace std;

namespace vg {

class VG;

// Consolidate bubble finding code here for both cactus and superbubbles
// to keep vg class size from getting even more out of hand

// SUPERBUBBLES

struct SB_Input{
    int num_vertices;
    vector<pair<id_t, id_t> > edges;
};

SB_Input vg_to_sb_input(VG& graph);
vector<pair<id_t, id_t> > get_superbubbles(SB_Input sbi);
vector<pair<id_t, id_t> > get_superbubbles(VG& graph);
map<pair<id_t, id_t>, vector<id_t> > superbubbles(VG& graph);

// ULTRA BUBBLES
// todo : refactor harmonize interface with stuff in deconstruct
// and superbubbles in general
struct Bubble {
    NodeSide start;
    NodeSide end;
    vector<id_t> contents;
    // cactus now gives us chaining information, stick here for now
    // so chain_offsets[i]-chain_offsets[i+1] mark the range
    // of children in chain i.  existing code that doesn't use
    // chains will be unaffected. 
    vector<int> chain_offsets;
    bool acyclic;
};

typedef Tree<Bubble> BubbleTree;

// Convert VG to Cactus Graph
// Notes:
//  - returned cactus graph needs to be freed by stCactusGraph_destruct
//  - returns "root" node as well as graph
pair<stCactusGraph*, stCactusNode*> vg_to_cactus(VG& graph);

// Return the hierchical cactus decomposition
// The root node is meaningless.  Its children are the top level chains.
// The returned tree must be deleted
BubbleTree* ultrabubble_tree(VG& graph);

// Enumerate ultra bubbles.  Interface (and output on DAGs)
// identical to superbubbles()
// Note: input graph will be sorted (as done for superbubbles())
map<pair<id_t, id_t>, vector<id_t> > ultrabubbles(VG& graph);

// Convert back from Cactus to VG
// (to, for example, display using vg view)
// todo: also provide mapping info to get nodes embedded in cactus components
VG cactus_to_vg(stCactusGraph* cactus_graph);

// Convert vg into vg formatted cactus representation
// Input graph must be sorted!
VG cactusify(VG& graph);

}


#endif
