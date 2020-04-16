#include "gapless_extender.hpp"

#include <algorithm>
#include <cstring>
#include <queue>
#include <set>
#include <stack>

namespace vg {

//------------------------------------------------------------------------------

// Numerical class constants.

constexpr size_t GaplessExtender::MAX_MISMATCHES;

//------------------------------------------------------------------------------

bool GaplessExtension::contains(const gbwtgraph::GBWTGraph& graph, seed_type seed) const {
    handle_t expected_handle = GaplessExtender::get_handle(seed);
    size_t expected_node_offset = GaplessExtender::get_node_offset(seed);
    size_t expected_read_offset = GaplessExtender::get_read_offset(seed);

    size_t read_offset = this->read_interval.first;
    size_t node_offset = this->offset;
    for (handle_t handle : this->path) {
        size_t len = graph.get_length(handle) - node_offset;
        read_offset += len;
        node_offset += len;
        if (handle == expected_handle && read_offset - expected_read_offset == node_offset - expected_node_offset) {
            return true;
        }
        node_offset = 0;
    }

    return false;
}

Position GaplessExtension::starting_position(const gbwtgraph::GBWTGraph& graph) const {
    Position position;
    if (this->empty()) {
        return position;
    }

    position.set_node_id(graph.get_id(this->path.front()));
    position.set_is_reverse(graph.get_is_reverse(this->path.front()));
    position.set_offset(this->offset);

    return position;
}

Position GaplessExtension::tail_position(const gbwtgraph::GBWTGraph& graph) const {
    Position position;
    if (this->empty()) {
        return position;
    }

    position.set_node_id(graph.get_id(this->path.back()));
    position.set_is_reverse(graph.get_is_reverse(this->path.back()));
    position.set_offset(this->tail_offset(graph));

    return position;
}

size_t GaplessExtension::tail_offset(const gbwtgraph::GBWTGraph& graph) const {
    size_t result = this->offset + this->length();
    for (size_t i = 0; i + 1 < this->path.size(); i++) {
        result -= graph.get_length(this->path[i]);
    }
    return result;
}

Path GaplessExtension::to_path(const gbwtgraph::GBWTGraph& graph, const std::string& sequence) const {

    Path result;

    auto mismatch = this->mismatch_positions.begin(); // The next mismatch.
    size_t read_offset = this->read_interval.first;   // Current offset in the read.
    size_t node_offset = this->offset;                // Current offset in the current node.
    for (size_t i = 0; i < this->path.size(); i++) {
        size_t limit = std::min(read_offset + graph.get_length(this->path[i]) - node_offset, this->read_interval.second);
        Mapping& mapping = *(result.add_mapping());
        mapping.mutable_position()->set_node_id(graph.get_id(this->path[i]));
        mapping.mutable_position()->set_offset(node_offset);
        mapping.mutable_position()->set_is_reverse(graph.get_is_reverse(this->path[i]));
        while (mismatch != this->mismatch_positions.end() && *mismatch < limit) {
            if (read_offset < *mismatch) {
                Edit& exact_match = *(mapping.add_edit());
                exact_match.set_from_length(*mismatch - read_offset);
                exact_match.set_to_length(*mismatch - read_offset);
            }
            Edit& edit = *(mapping.add_edit());
            edit.set_from_length(1);
            edit.set_to_length(1);
            edit.set_sequence(std::string(1, sequence[*mismatch]));
            read_offset = *mismatch + 1;
            ++mismatch;
        }
        if (read_offset < limit) {
            Edit& exact_match = *(mapping.add_edit());
            exact_match.set_from_length(limit - read_offset);
            exact_match.set_to_length(limit - read_offset);
            read_offset = limit;
        }
        mapping.set_rank(i + 1);
        node_offset = 0;
    }

    return result;
}

//------------------------------------------------------------------------------

GaplessExtender::GaplessExtender() :
    graph(nullptr), aligner(nullptr)
{
}

GaplessExtender::GaplessExtender(const gbwtgraph::GBWTGraph& graph, const Aligner& aligner) :
    graph(&graph), aligner(&aligner)
{
}

//------------------------------------------------------------------------------

template<class Element>
void in_place_subvector(std::vector<Element>& vec, size_t head, size_t tail) {
    if (head >= tail || tail > vec.size()) {
        vec.clear();
        return;
    }
    if (head > 0) {
        for (size_t i = head; i < tail; i++) {
            vec[i - head] = std::move(vec[i]);
        }
    }
    vec.resize(tail - head);
}

// Compute the score based on read_interval, internal_score, left_full, and right_full.
void set_score(GaplessExtension& extension, const Aligner* aligner) {
    // Assume that everything matches.
    extension.score = static_cast<int32_t>((extension.read_interval.second - extension.read_interval.first) * aligner->match);
    // Handle the mismatches.
    extension.score -= static_cast<int32_t>(extension.internal_score * (aligner->match + aligner->mismatch));
    // Handle full-length bonuses.
    extension.score += static_cast<int32_t>(extension.left_full * aligner->full_length_bonus);
    extension.score += static_cast<int32_t>(extension.right_full * aligner->full_length_bonus); 
}

// Match the initial node, assuming that read_offset or node_offset is 0.
// Updates internal_score and old_score; use set_score() to compute score.
void match_initial(GaplessExtension& match, const std::string& seq, gbwtgraph::GBWTGraph::view_type target) {
    size_t node_offset = match.offset;
    size_t left = std::min(seq.length() - match.read_interval.second, target.second - node_offset);
    while (left > 0) {
        size_t len = std::min(left, sizeof(std::uint64_t));
        std::uint64_t a = 0, b = 0;
        std::memcpy(&a, seq.data() + match.read_interval.second, len);
        std::memcpy(&b, target.first + node_offset, len);
        if (a == b) {
            match.read_interval.second += len;
            node_offset += len;
        } else {
            for (size_t i = 0; i < len; i++) {
                if (seq[match.read_interval.second] != target.first[node_offset]) {
                    match.internal_score++;
                }
                match.read_interval.second++;
                node_offset++;
            }
        }
        left -= len;
    }
    match.old_score = match.internal_score;
}

// Match forward but stop before the mismatch count reaches the limit.
// Updates internal_score; use set_score() to recompute score.
// Returns the tail offset (the number of characters matched).
size_t match_forward(GaplessExtension& match, const std::string& seq, gbwtgraph::GBWTGraph::view_type target, uint32_t mismatch_limit) {
    size_t node_offset = 0;
    size_t left = std::min(seq.length() - match.read_interval.second, target.second - node_offset);
    while (left > 0) {
        size_t len = std::min(left, sizeof(std::uint64_t));
        std::uint64_t a = 0, b = 0;
        std::memcpy(&a, seq.data() + match.read_interval.second, len);
        std::memcpy(&b, target.first + node_offset, len);
        if (a == b) {
            match.read_interval.second += len;
            node_offset += len;
        } else {
            for (size_t i = 0; i < len; i++) {
                if (seq[match.read_interval.second] != target.first[node_offset]) {
                    if (match.internal_score + 1 >= mismatch_limit) {
                        return node_offset;
                    }
                    match.internal_score++;
                }
                match.read_interval.second++;
                node_offset++;
            }
        }
        left -= len;
    }
    return node_offset;
}

// Match forward but stop before the mismatch count reaches the limit.
// Starts from the offset in the match and updates it.
// Updates internal_score; use set_score() to recompute score.
void match_backward(GaplessExtension& match, const std::string& seq, gbwtgraph::GBWTGraph::view_type target, uint32_t mismatch_limit) {
    size_t left = std::min(match.read_interval.first, match.offset);
    while (left > 0) {
        size_t len = std::min(left, sizeof(std::uint64_t));
        std::uint64_t a = 0, b = 0;
        std::memcpy(&a, seq.data() + match.read_interval.first - len, len);
        std::memcpy(&b, target.first + match.offset - len, len);
        if (a == b) {
            match.read_interval.first -= len;
            match.offset -= len;
        } else {
            for (size_t i = 0; i < len; i++) {
                if (seq[match.read_interval.first - 1] != target.first[match.offset - 1]) {
                    if (match.internal_score + 1 >= mismatch_limit) {
                        return;
                    }
                    match.internal_score++;
                }
                match.read_interval.first--;
                match.offset--;
            }
        }
        left -= len;
    }
}

// Sort the extensions from left to right. Remove duplicates and empty extensions.
void remove_duplicates(std::vector<GaplessExtension>& result) {
    auto sort_order = [](const GaplessExtension& a, const GaplessExtension& b) -> bool {
        if (a.read_interval != b.read_interval) {
            return (a.read_interval < b.read_interval);
        }
        if (a.state.backward.node != b.state.backward.node) {
            return (a.state.backward.node < b.state.backward.node);
        }
        if (a.state.forward.node != b.state.forward.node) {
            return (a.state.forward.node < b.state.forward.node);
        }
        return (a.state.backward.range < b.state.backward.range);
    };
    std::sort(result.begin(), result.end(), sort_order);
    size_t tail = 0;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].empty()) {
            continue;
        }
        if (tail == 0 || result[i] != result[tail - 1]) {
            if (i > tail) {
                result[tail] = std::move(result[i]);
            }
            tail++;
        }
    }
    result.resize(tail);
}

// Realign the extensions to find the mismatching positions.
void find_mismatches(const std::string& seq, const gbwtgraph::GBWTGraph& graph, std::vector<GaplessExtension>& result) {
    for (GaplessExtension& extension : result) {
        if (extension.internal_score == 0) {
            continue;
        }
        extension.mismatch_positions.reserve(extension.internal_score);
        size_t node_offset = extension.offset, read_offset = extension.read_interval.first;
        for (const handle_t& handle : extension.path) {
            gbwtgraph::GBWTGraph::view_type target = graph.get_sequence_view(handle);
            while (node_offset < target.second && read_offset < extension.read_interval.second) {
                if (target.first[node_offset] != seq[read_offset]) {
                    extension.mismatch_positions.push_back(read_offset);
                }
                node_offset++;
                read_offset++;
            }
            node_offset = 0;
        }
    }
}

size_t interval_length(std::pair<size_t, size_t> interval) {
    return interval.second - interval.first;
}

std::vector<handle_t> get_path(const std::vector<handle_t>& first, handle_t second) {
    std::vector<handle_t> result;
    result.reserve(first.size() + 1);
    result.insert(result.end(), first.begin(), first.end());
    result.push_back(second);
    return result;
}

std::vector<handle_t> get_path(handle_t first, const std::vector<handle_t>& second) {
    std::vector<handle_t> result;
    result.reserve(second.size() + 1);
    result.push_back(first);
    result.insert(result.end(), second.begin(), second.end());
    return result;
}

std::vector<handle_t> get_path(const std::vector<handle_t>& first, gbwt::node_type second) {
    return get_path(first, gbwtgraph::GBWTGraph::node_to_handle(second));
}

std::vector<handle_t> get_path(gbwt::node_type reverse_first, const std::vector<handle_t>& second) {
    return get_path(gbwtgraph::GBWTGraph::node_to_handle(gbwt::Node::reverse(reverse_first)), second);
}

//------------------------------------------------------------------------------

std::vector<GaplessExtension> GaplessExtender::extend(cluster_type& cluster, const std::string& sequence, size_t max_mismatches, bool trim_extensions) const {

    std::vector<GaplessExtension> result;
    if (this->graph == nullptr || this->aligner == nullptr || sequence.empty()) {
        return result;
    }

    // Allocate a GBWT record cache.
    gbwt::CachedGBWT cache = this->graph->get_cache();

    // Find either the best extension for each seed or the best two full-length alignments
    // for the entire cluster. The second-best full-length alignment has full_length_mismatches
    // mismatches; we are not interested in extensions with this many mismatches anymore.
    bool full_length_found = false;
    GaplessExtension best_alignment;
    GaplessExtension second_best_alignment;
    uint32_t full_length_mismatches = std::numeric_limits<uint32_t>::max();
    for (seed_type seed : cluster) {

        // Check if the seed is contained in an exact full-length alignment.
        if (full_length_found && best_alignment.internal_score == 0) {
            if (best_alignment.contains(*(this->graph), seed)) {
                continue;
            }
        }

        GaplessExtension best_match {
            { }, static_cast<size_t>(0), gbwt::BidirectionalState(),
            { static_cast<size_t>(0), static_cast<size_t>(0) }, { },
            std::numeric_limits<int32_t>::min(), false, false,
            false, false, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()
        };

        // Match the initial node and add it to the queue, unless we already have
        // two at least as good full-length alignments.
        std::priority_queue<GaplessExtension> extensions;
        {
            size_t read_offset = get_read_offset(seed);
            size_t node_offset = get_node_offset(seed);
            GaplessExtension match {
                { seed.first }, node_offset, this->graph->get_bd_state(cache, seed.first),
                { read_offset, read_offset }, { },
                static_cast<int32_t>(0), false, false,
                false, false, static_cast<uint32_t>(0), static_cast<uint32_t>(0)
            };
            match_initial(match, sequence, this->graph->get_sequence_view(seed.first));
            if (match.internal_score >= full_length_mismatches) {
                continue;
            }
            if (match.read_interval.first == 0) {
                match.left_full = true;
                match.left_maximal = true;
            }
            if (match.read_interval.second >= sequence.length()) {
                match.right_full = true;
                match.right_maximal = true;
            }
            set_score(match, this->aligner);
            extensions.push(std::move(match));
        }

        // Extend the most promising extensions first, using alignment scores for priority.
        // First make the extension right-maximal and then left-maximal.
        while (!extensions.empty()) {
            GaplessExtension curr = std::move(extensions.top());
            extensions.pop();
            if (curr.internal_score >= full_length_mismatches) {
                continue;
            }

            // Case 1: Extend to the right.
            if (!curr.right_maximal) {
                bool found_extension = false;
                // Always allow at least max_mismatches / 2 mismatches in the current flank.
                uint32_t mismatch_limit = std::max(
                    static_cast<uint32_t>(max_mismatches + 1),
                    static_cast<uint32_t>(max_mismatches / 2 + curr.old_score + 1));
                mismatch_limit = std::min(mismatch_limit, full_length_mismatches);
                this->graph->follow_paths(cache, curr.state, false, [&](const gbwt::BidirectionalState& next_state) -> bool {
                    handle_t handle = gbwtgraph::GBWTGraph::node_to_handle(next_state.forward.node);
                    GaplessExtension next {
                        { }, curr.offset, next_state,
                        curr.read_interval, { },
                        curr.score, curr.left_full, curr.right_full,
                        curr.left_maximal, curr.right_maximal, curr.internal_score, curr.old_score
                    };
                    size_t node_offset = match_forward(next, sequence, this->graph->get_sequence_view(handle), mismatch_limit);
                    if (node_offset == 0) { // Did not match anything.
                        return true;
                    }
                    next.path = get_path(curr.path, handle);
                    // Did the extension become right-maximal?
                    if (next.read_interval.second >= sequence.length()) {
                        next.right_full = true;
                        next.right_maximal = true;
                        next.old_score = next.internal_score;
                    } else if (node_offset < this->graph->get_length(handle)) {
                        next.right_maximal = true;
                        next.old_score = next.internal_score;
                    }
                    set_score(next, this->aligner);
                    extensions.push(std::move(next));
                    found_extension = true;
                    return true;
                });
                if (!found_extension) {
                    curr.right_maximal = true;
                    curr.old_score = curr.internal_score;
                } else {
                    continue;
                }
            }

            // Case 2: Extend to the left.
            if (!curr.left_maximal) {
                bool found_extension = false;
                // Always allow at least max_mismatches / 2 mismatches in the current flank.
                uint32_t mismatch_limit = std::max(
                    static_cast<uint32_t>(max_mismatches + 1),
                    static_cast<uint32_t>(max_mismatches / 2 + curr.old_score + 1));
                mismatch_limit = std::min(mismatch_limit, full_length_mismatches);
                this->graph->follow_paths(cache, curr.state, true, [&](const gbwt::BidirectionalState& next_state) -> bool {
                    handle_t handle = gbwtgraph::GBWTGraph::node_to_handle(gbwt::Node::reverse(next_state.backward.node));
                    size_t node_length = this->graph->get_length(handle);
                    GaplessExtension next {
                        { }, node_length, next_state,
                        curr.read_interval, { },
                        curr.score, curr.left_full, curr.right_full,
                        curr.left_maximal, curr.right_maximal, curr.internal_score, curr.old_score
                    };
                    match_backward(next, sequence, this->graph->get_sequence_view(handle), mismatch_limit);
                    if (next.offset >= node_length) { // Did not match anything.
                        return true;
                    }
                    next.path = get_path(handle, curr.path);
                    // Did the extension become left-maximal?
                    if (next.read_interval.first == 0) {
                        next.left_full = true;
                        next.left_maximal = true;
                        // No need to set old_score.
                    } else if (next.offset > 0) {
                        next.left_maximal = true;
                        // No need to set old_score.
                    }
                    set_score(next, this->aligner);
                    extensions.push(std::move(next));
                    found_extension = true;
                    return true;
                });
                if (!found_extension) {
                    curr.left_maximal = true;
                    // No need to set old_score.
                } else {
                    continue;
                }
            }

            // Case 3: Maximal extension with a better score than the best extension so far.
            if (best_match < curr) {
                best_match = std::move(curr);
            }
        }

        // Handle the best match. If we have a full-length alignment, check if it is among
        // the best two we have found so far. Otherwise add the partial extension to the
        // result, if we do not have full-length alignments.
        if (best_match.full() && best_match.internal_score <= max_mismatches) {
            full_length_found = true;
            if (best_alignment.empty() || best_match.internal_score < best_alignment.internal_score) {
                second_best_alignment = std::move(best_alignment);
                best_alignment = std::move(best_match);
            }
            // If the best alignment contains mismatches, we may reach it from multiple seeds.
            // Make sure that we do not report it as the best and the second best alignment.
            else if ((second_best_alignment.empty() || best_match.internal_score < second_best_alignment.internal_score) && best_match != best_alignment) {
                second_best_alignment = std::move(best_match);
                full_length_mismatches = second_best_alignment.internal_score;
            }
            // We can stop the search, because we have two exact full-length alignments.
            if (full_length_mismatches == 0) {
                break;
            }
        } else if (!full_length_found && !best_match.empty()) {
            result.emplace_back(std::move(best_match));
        }
    }

    if (full_length_found) {
        result.clear();
        result.emplace_back(std::move(best_alignment));
        if (!second_best_alignment.empty()) {
            result.emplace_back(std::move(second_best_alignment));
        }
    }

    // Remove duplicates, find mismatches, and trim mismatches to maximize score.
    // If we have a full-length alignment with sufficiently few mismatches, we do
    // not trim it.
    remove_duplicates(result);
    find_mismatches(sequence, *(this->graph), result);
    if (trim_extensions) {
        this->trim(result, max_mismatches, &cache);
    }

    return result;
}

//------------------------------------------------------------------------------

// Trim mismatches from the extension to maximize the score. Returns true if the
// extension was trimmed.
bool trim_mismatches(GaplessExtension& extension, const gbwtgraph::GBWTGraph& graph, const gbwt::CachedGBWT& cache, const Aligner& aligner) {

    if (extension.exact()) {
        return false;
    }

    // Start with the initial run of matches.
    auto mismatch = extension.mismatch_positions.begin();
    std::pair<size_t, size_t> current_interval(extension.read_interval.first, *mismatch);
    int32_t current_score = interval_length(current_interval) * aligner.match;
    if (extension.left_full) {
        current_score += aligner.full_length_bonus;
    }

    // Process the alignment and keep track of the best interval we have seen so far.
    std::pair<size_t, size_t> best_interval = current_interval;
    int32_t best_score = current_score;
    while (mismatch != extension.mismatch_positions.end()) {
        // See if we should start a new interval after the mismatch.
        if (current_score >= aligner.mismatch) {
            current_interval.second++;
            current_score -= aligner.mismatch;
        } else {
            current_interval.first = current_interval.second = *mismatch + 1;
            current_score = 0;
        }
        ++mismatch;

        // Process the following run of matches.
        if (mismatch == extension.mismatch_positions.end()) {
            size_t length = extension.read_interval.second - current_interval.second;
            current_interval.second = extension.read_interval.second;
            current_score += length * aligner.match;
            if (extension.right_full) {
                current_score += aligner.full_length_bonus;
            }
        } else {
            size_t length = *mismatch - current_interval.second;
            current_interval.second = *mismatch;
            current_score += length * aligner.match;
        }

        // Update the best interval.
        if (current_score > best_score || (current_score == best_score && interval_length(current_interval) > interval_length(best_interval))) {
            best_interval = current_interval;
            best_score = current_score;
        }
    }

    // Special cases: no trimming or complete trimming.
    if (best_interval == extension.read_interval) {
        return false;
    }
    if (interval_length(best_interval) == 0) {
        extension.path.clear();
        extension.read_interval = best_interval;
        extension.mismatch_positions.clear();
        extension.score = 0;
        extension.left_full = extension.right_full = false;
        return true;
    }

    // Update alignment statistics.
    bool path_changed = false;
    if (best_interval.first > extension.read_interval.first) {
        extension.left_full = false;
    }
    if (best_interval.second < extension.read_interval.second) {
        extension.right_full = false;
    }
    size_t node_offset = extension.offset, read_offset = extension.read_interval.first;
    extension.read_interval = best_interval;
    extension.score = best_score;

    // Trim the path.
    size_t head = 0;
    while (head < extension.path.size()) {
        size_t node_length = graph.get_length(extension.path[head]);
        read_offset += node_length - node_offset;
        node_offset = 0;
        if (read_offset > extension.read_interval.first) {
            extension.offset = node_length - (read_offset - extension.read_interval.first);
            break;
        }
        head++;
    }
    size_t tail = head + 1;
    while (read_offset < extension.read_interval.second) {
        read_offset += graph.get_length(extension.path[tail]);
        tail++;
    }
    if (head > 0 || tail < extension.path.size()) {
        in_place_subvector(extension.path, head, tail);
        extension.state = graph.bd_find(cache, extension.path);
    }

    // Trim the mismatches.
    head = 0;
    while (head < extension.mismatch_positions.size() && extension.mismatch_positions[head] < extension.read_interval.first) {
        head++;
    }
    tail = head;
    while (tail < extension.mismatch_positions.size() && extension.mismatch_positions[tail] < extension.read_interval.second) {
        tail++;
    }
    in_place_subvector(extension.mismatch_positions, head, tail);

    return true;
}

void GaplessExtender::trim(std::vector<GaplessExtension>& extensions, size_t max_mismatches, const gbwt::CachedGBWT* cache) const {

    // Allocate a cache if we were not provided with one.
    bool free_cache = (cache == nullptr);
    if (free_cache) {
        cache = new gbwt::CachedGBWT(this->graph->get_cache());
    }

    bool trimmed = false;
    for (GaplessExtension& extension : extensions) {
        if (!extension.full() || extension.mismatches() > max_mismatches) {
            trimmed |= trim_mismatches(extension, *(this->graph), *cache, *(this->aligner));
        }
    }
    if (trimmed) {
        remove_duplicates(extensions);
    }

    // Free the cache if we allocated it.
    if (free_cache) {
        delete cache;
        cache = nullptr;
    }
}

//------------------------------------------------------------------------------

struct state_hash {
    size_t operator()(const gbwt::BidirectionalState& state) const {
        size_t result = wang_hash_64(state.forward.node);
        result ^= wang_hash_64(state.forward.range.first) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= wang_hash_64(state.forward.range.second) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= wang_hash_64(state.backward.node) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= wang_hash_64(state.backward.range.first) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= wang_hash_64(state.backward.range.second) + 0x9e3779b9 + (result << 6) + (result >> 2);
        return result;
    }
};

void GaplessExtender::unfold_haplotypes(const SubHandleGraph& subgraph, std::vector<std::vector<handle_t>>& haplotype_paths,  bdsg::HashGraph& unfolded) const {

    // A state and its reverse complement are equivalent.
    auto get_key = [](gbwt::BidirectionalState state) -> gbwt::BidirectionalState {
        if ((state.backward.node < state.forward.node) ||
            (state.backward.node == state.forward.node && state.backward.range < state.forward.range)) {
            state.flip();
        }
        return state;
    };

    // Find the nodes where paths start/end or enter/exit the subgraph. Extend these states
    // to the other direction. Checking for starts/enters would be enough if we are sure
    // that there are no weird orientation flips.
    gbwt::CachedGBWT cache = this->graph->get_cache();
    std::stack<std::pair<gbwt::BidirectionalState, std::vector<handle_t>>> states;
    subgraph.for_each_handle([&](const handle_t& handle) {
        gbwt::BidirectionalState state = this->graph->get_bd_state(cache, handle);
        size_t forward_covered = 0, backward_covered = 0;
        this->graph->follow_paths(cache, state, false, [&](const gbwt::BidirectionalState& next) -> bool {
            if (subgraph.has_node(gbwt::Node::id(next.forward.node))) {
                forward_covered += next.size();
            }
            return true;
        });
        this->graph->follow_paths(cache, state, true, [&](const gbwt::BidirectionalState& prev) -> bool {
            if (subgraph.has_node(gbwt::Node::id(prev.backward.node))) {
                backward_covered += prev.size();
            }
            return true;
        });
        if (backward_covered < state.size()) {
            std::vector<handle_t> path = { handle };
            states.push(std::make_pair(state, path)); // Left-maximal.
        }
        if (forward_covered < state.size()) {
            std::vector<handle_t> path = { this->graph->flip(handle) };
            state.flip();
            states.push(std::make_pair(state, path)); // Right-maximal.
        }
    }, false);

    // Extend the states as far forward as possible within the subgraph. Because we usually find
    // each haplotype twice, we store the set of states we have reported so far.
    spp::sparse_hash_set<gbwt::BidirectionalState, state_hash> reported;
    while (!states.empty()) {
        gbwt::BidirectionalState state;
        std::vector<handle_t> path;
        std::tie(state, path) = states.top();
        states.pop();
        bool found_extension = false;
        size_t covered_size = 0;
        this->graph->follow_paths(cache, state, false, [&](const gbwt::BidirectionalState& next) -> bool {
            if (!subgraph.has_node(gbwt::Node::id(next.forward.node))) {
                return true;
            }
            found_extension = true;
            states.push(std::make_pair(next, get_path(path, next.forward.node)));
            return true;
        });
        // Report the path if we did not find any extensions.
        if (!found_extension) {
            gbwt::BidirectionalState key = get_key(state);
            if (reported.find(key) == reported.end()) {
                reported.insert(key);
                haplotype_paths.push_back(path);
                size_t result_size = 0;
                for (handle_t handle : path) {
                    result_size += this->graph->get_length(handle);
                }
                std::string sequence;
                sequence.reserve(result_size);
                for (handle_t handle : path) {
                    gbwtgraph::GBWTGraph::view_type view = this->graph->get_sequence_view(handle);
                    sequence.insert(sequence.size(), view.first, view.second);
                }
                unfolded.create_handle(sequence, 2 * haplotype_paths.size() - 1);
                unfolded.create_handle(reverse_complement(sequence), 2 * haplotype_paths.size());
            }
        }
    }
}

//------------------------------------------------------------------------------

void GaplessExtender::transform_alignment(Alignment& aln, const std::vector<std::vector<handle_t>>& haplotype_paths) const {

    if (aln.path().mapping_size() != 1) {
        return;
    }

    // Find the original path and reverse it if necessary.
    Mapping& source = *(aln.mutable_path()->mutable_mapping(0));
    nid_t id = source.position().node_id();
    std::vector<handle_t> original_path = haplotype_paths[(id - 1) / 2];
    if ((id & 1) == 0) {
        std::reverse(original_path.begin(), original_path.end());
        for (handle_t& handle : original_path) {
            handle = this->graph->flip(handle);
        }
    }

    Path result;
    size_t source_offset = 0; // In the source node.
    size_t aln_offset = 0; // In the aligned sequence.
    size_t node_offset = source.position().offset(); // In the current target node.
    size_t edit_id = 0; // In the alignment to the source.
    for (handle_t handle : original_path) {

        // Skip nodes before the alignment.
        size_t node_length = this->graph->get_length(handle);
        if (source_offset + node_length <= source.position().offset()) {
            source_offset += node_length;
            node_offset -= node_length;
            continue;
        }

        // This node is part of the alignment.
        Mapping& mapping = *(result.add_mapping());
        mapping.mutable_position()->set_node_id(this->graph->get_id(handle));
        mapping.mutable_position()->set_offset(node_offset);
        mapping.mutable_position()->set_is_reverse(this->graph->get_is_reverse(handle));

        // Handle all edits covering the node.
        while (node_offset < node_length && aln_offset < aln.sequence().length()) {
            Edit& source_edit = *(source.mutable_edit(edit_id));
            Edit& target_edit = *(mapping.add_edit());
            if (source_edit.from_length() == 0) { // Insertion in the read.
                target_edit.set_to_length(source_edit.to_length());
                target_edit.set_sequence(source_edit.sequence());
                aln_offset += source_edit.to_length();
                edit_id++;
            } else if (source_edit.to_length() == 0) { // Deletion in the read.
                size_t deletion_length = std::min(static_cast<size_t>(source_edit.from_length()), node_length - node_offset);
                target_edit.set_from_length(deletion_length);
                node_offset += deletion_length;
                if (deletion_length == source_edit.from_length()) {
                    edit_id++;
                } else {
                    source_edit.set_from_length(source_edit.from_length() - deletion_length);
                }
            } else { // Match or mismatch.
                assert(source_edit.from_length() == source_edit.to_length());
                size_t match_length = std::min(static_cast<size_t>(source_edit.from_length()), node_length - node_offset);
                target_edit.set_from_length(match_length);
                target_edit.set_to_length(match_length);
                aln_offset += match_length;
                node_offset += match_length;
                if (source_edit.sequence().length() > 0) { // Mismatch.
                    target_edit.set_sequence(source_edit.sequence().substr(0, match_length));
                }
                if (match_length == source_edit.from_length()) {
                    edit_id++;
                } else {
                    source_edit.set_from_length(source_edit.from_length() - match_length);
                    source_edit.set_to_length(source_edit.to_length() - match_length);
                    if (source_edit.sequence().length() > 0) {
                        source_edit.set_sequence(source_edit.sequence().substr(match_length));
                    }
                }
            }
        }
        source_offset += node_length;
        node_offset = 0;

        // We are at the end.
        if (aln_offset >= aln.sequence().length()) {
            break;
        }
    }

    // FIXME tests

    *(aln.mutable_path()) = std::move(result);
}

//------------------------------------------------------------------------------

} // namespace vg
