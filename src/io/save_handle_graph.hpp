// SPDX-FileCopyrightText: 2014 Erik Garrison
//
// SPDX-License-Identifier: MIT

#ifndef VG_IO_SAVE_HANDLE_GRAPH_HPP_INCLUDED
#define VG_IO_SAVE_HANDLE_GRAPH_HPP_INCLUDED

/**
 * \file save_handle_graph.hpp
 * Use vpkg to serialize a HandleGraph object
 */

#include <handlegraph/serializable_handle_graph.hpp>
#include "bdsg/packed_graph.hpp"
#include "bdsg/hash_graph.hpp"
#include "bdsg/odgi.hpp"
#include "vg.hpp"
#include "xg.hpp"
#include <vg/io/stream.hpp>
#include <vg/io/vpkg.hpp>

#include <memory>

namespace vg {

namespace io {

using namespace std;


/**
 * Save a handle graph. 
 * Todo: should this be somewhere else (ie in vgio with new types registered?)
 */
inline void save_handle_graph(HandleGraph* graph, ostream& os) {
    
    if (dynamic_cast<SerializableHandleGraph*>(graph) != nullptr) {
        // SerializableHandleGraphs are all serialized bare, without VPKG framing, for libbdsg compatibility.
        dynamic_cast<SerializableHandleGraph*>(graph)->serialize(os);
    } else if (dynamic_cast<VG*>(graph) != nullptr) {
        // vg::VG doesn't use a magic number and isn't a SerializableHandleGraph
        vg::io::VPKG::save(*dynamic_cast<VG*>(graph), os);
    } else {
        throw runtime_error("Internal error: unable to serialize graph");
    }
}

inline void save_handle_graph(HandleGraph* graph, const string& dest_path) {
    if (dynamic_cast<SerializableHandleGraph*>(graph) != nullptr) {
        // SerializableHandleGraphs are all serialized bare, without VPKG framing, for libbdsg compatibility.
        dynamic_cast<SerializableHandleGraph*>(graph)->serialize(dest_path);
    } else if (dynamic_cast<VG*>(graph) != nullptr) {
        // vg::VG doesn't use a magic number and isn't a SerializableHandleGraph
        vg::io::VPKG::save(*dynamic_cast<VG*>(graph), dest_path);
    } else {
        throw runtime_error("Internal error: unable to serialize graph");
    }    
}

// Check that output format specifier is a valid graph type
inline bool valid_output_format(const string& fmt_string) {
    return fmt_string == "vg" || fmt_string == "pg" || fmt_string == "hg";
}

// Create a new graph (of handle graph type T) where the implementation is chosen using the format string
template<class T>
unique_ptr<T> new_output_graph(const string& fmt_string) {
    if (fmt_string == "vg") {
        return make_unique<VG>();
    } else if (fmt_string == "pg") {
        return make_unique<bdsg::PackedGraph>();
    } else if (fmt_string == "hg") {
        return make_unique<bdsg::HashGraph>();
    } else {
        return unique_ptr<T>();
    }
}

}

}

#endif
