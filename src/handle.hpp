#ifndef VG_HANDLE_HPP_INCLUDED
#define VG_HANDLE_HPP_INCLUDED

/** \file 
 * Defines a handle type that can refer to oriented nodes of, and be used to
 * traverse, any backing graph implementation. Not just an ID or a pos_t because
 * XG (and maybe other implementations) provide more efficient local traversal
 * mechanisms if you can skip ID lookups.
 */

#include "types.hpp"
#include <functional>
#include <cstdint>

namespace vg {

using namespace std;

/// A handle is 8 (assuming id_t is still int64_t) opaque bytes.
/// A handle refers to an oriented node.
/// Two handles are equal iff they refer to the same orientation of the same node in the same graph.
/// Handles have no ordering, but can be hashed.
struct handle_t {
    char data[sizeof(id_t)];
};

// XG is going to store node index in its g-vector and node orientation in there
// VG is going to store ID and orientation
// Other implementations can store other things (or maybe int indexes into tables)

/// View a handle as an integer
inline int64_t& as_integer(handle_t& handle) {
    return reinterpret_cast<int64_t&>(handle);
}

/// View a const handle as a const integer
inline const int64_t& as_integer(const handle_t& handle) {
    return reinterpret_cast<const int64_t&>(handle);
}

/// View an integer as a handle
inline handle_t& as_handle(int64_t& value) {
    return reinterpret_cast<handle_t&>(value);
}

/// View a const integer as a const handle
inline const handle_t& as_handle(const int64_t& value) {
    return reinterpret_cast<const handle_t&>(value);
}

/// Define equality on handles
inline bool operator==(const handle_t& a, const handle_t& b) {
    return as_integer(a) == as_integer(b);
}

/// Define inequality on handles
inline bool operator!=(const handle_t& a, const handle_t& b) {
    return as_integer(a) != as_integer(b);
}

}

// This needs to be outside the vg namespace

namespace std {

/**
 * Define hashes for handles.
 */
template<> struct hash<vg::handle_t> {
public:
    inline size_t operator()(const vg::handle_t& handle) const {
        return std::hash<int64_t>()(vg::as_integer(handle));
    }
};

}

namespace vg {

using namespace std;

/**
 * This is the interface that a graph that uses handles needs to support.
 * It is also the interface that users should code against.
 */
class HandleGraph {
public:
    /// Look up the handle for the node with the given ID in the given orientation
    virtual handle_t get_handle(const id_t& node_id, bool is_reverse = false) const = 0;
    
    /// Get the ID from a handle
    virtual id_t get_id(const handle_t& handle) const = 0;
    
    /// Get the orientation of a handle
    virtual bool get_is_reverse(const handle_t& handle) const = 0;
    
    /// Invert the orientation of a handle (potentially without getting its ID)
    virtual handle_t flip(const handle_t& handle) const = 0;
    
    /// Get the length of a node
    virtual size_t get_length(const handle_t& handle) const = 0;
    
    /// Get the sequence of a node, presented in the handle's local forward
    /// orientation.
    virtual string get_sequence(const handle_t& handle) const = 0;
    
    /// Loop over all the handles to next/previous (right/left) nodes. Passes
    /// them to a callback which returns false to stop iterating and true to
    /// continue.
    virtual void follow_edges(const handle_t& handle, bool go_left, const function<bool(const handle_t&)>& iteratee) const = 0;
    
    /// Loop over all the handles to next/previous (right/left) nodes. Works
    /// with a callback that just takes all the handles and returns void.
    template <typename T>
    auto follow_edges(const handle_t& handle, bool go_left, T&& iteratee) const
    -> typename std::enable_if<std::is_void<decltype(iteratee(get_handle(0, false)))>::value>::type {
        // Implementation only for void-returning iteratees
        // We ought to just overload on the std::function but that's not allowed until C++14.
        // See <https://stackoverflow.com/q/13811180>
        
        // We also can't use result_of<T(handle_t)>::type to sniff the return
        // type out because that ::type would not exist (since that's what you
        // get for a void apparently?) and we couldn't check if it's bool or
        // void.
        
        // So we do this nonsense thing with a trailing return type (to get the
        // actual arg into scope) and a decltype (which is allowed to resolve to
        // void) and is_void (which is allowed to take void) and a fake
        // get_handle call (which is the shortest handle_t-typed expression I
        // could think of).
        
        // Make a wrapper that puts a bool return type on.
        function<bool(const handle_t&)> lambda = [&](const handle_t& found) {
            iteratee(found);
            return true;
        };
        
        // Use that
        follow_edges(handle, go_left, lambda);
        
        // During development I managed to get earlier versions of this template to build infinitely recursive functions.
        static_assert(!std::is_void<decltype(lambda(get_handle(0, false)))>::value, "can't take our own lambda");
    }
    
    /// Get the locally forward version of a handle
    inline handle_t forward(const handle_t& handle) const {
        return this->get_is_reverse(handle) ? this->flip(handle) : handle;
    }
    
    // A pair of handles can be used as an edge. When so used, the handles have a
    // cannonical order and orientation.
    inline pair<handle_t, handle_t> edge_handle(const handle_t& left, const handle_t& right) const {
        // The degeneracy is between any pair and a pair of the same nodes but reversed in order and orientation.
        // We compare those two pairs and construct the smaller one.
        auto flipped_right = this->flip(right);
        
        if (as_integer(left) > as_integer(flipped_right)) {
            // The other orientation would be smaller.
            return make_pair(flipped_right, this->flip(left));
        } else if(as_integer(left) == as_integer(flipped_right)) {
            // Our left and the flipped pair's left would be equal.
            auto flipped_left = this->flip(left);
            if (as_integer(right) > as_integer(flipped_left)) {
                // And our right is too big, so flip.
                return make_pair(flipped_right, flipped_left);
            } else {
                // No difference or we're smaller.
                return make_pair(left, right);
            }
        } else {
            // We're smaller
            return make_pair(left, right);
        }
    }
};

}

#endif
