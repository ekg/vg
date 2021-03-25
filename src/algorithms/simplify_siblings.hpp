// SPDX-FileCopyrightText: 2014 Erik Garrison
//
// SPDX-License-Identifier: MIT

#ifndef VG_ALGORITHMS_SIMPLIFY_SIBLINGS_HPP_INCLUDED
#define VG_ALGORITHMS_SIMPLIFY_SIBLINGS_HPP_INCLUDED


#include "../handle.hpp"
#include "merge.hpp"
#include <handlegraph/mutable_path_deletable_handle_graph.hpp>

namespace vg {
namespace algorithms {

using namespace std;

/**
 * Simplify siblings in the given graph.
 *
 * When one base has two successors with the same base value, and those
 * successors have the same set of predecessors, the successors will be merged.
 *
 * Performs only a subset of the possible merges. Can only merge in from one
 * side of a given node in a single invocation. Returns true if it made
 * progress and there may be more merging to do.
 *
 * Preserves paths.
 */
bool simplify_siblings(handlegraph::MutablePathDeletableHandleGraph* graph);
    
}
}

#endif
