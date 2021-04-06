#include "../gbwt_helper.hpp"
#include "../handle.hpp"
#include "../subgraph.hpp"
#include "../vg.hpp"
#include <string>
#include <gbwtgraph/gbwtgraph.h>


namespace vg {
namespace algorithms {

class SnarlNormalizer {
  public:
    virtual ~SnarlNormalizer() = default;

    SnarlNormalizer(MutablePathDeletableHandleGraph &graph, const gbwtgraph::GBWTGraph &haploGraph,
                    const int &max_alignment_size = 200,
                    const string &path_finder = "GBWT" /*alternative is "exhaustive"*/);

    virtual void normalize_top_level_snarls(ifstream &snarl_stream);

    virtual vector<int> normalize_snarl(id_t source_id, id_t sink_id, const bool backwards);

  protected:
    // member variables:
    // the handle graph with snarls to normalize
    MutablePathDeletableHandleGraph &_graph;
    // GBWT graph with snarls to normalize, includes the embedded threads needed for the
    // GBWTPathFinder approach.
    const gbwtgraph::GBWTGraph &_haploGraph;
    // the maximum number of threads allowed to align in a given snarl. If the number of
    // threads exceeds this threshold, the snarl is skipped.
    int _max_alignment_size;
    const string &_path_finder;

    //////////////////////////////////////////////////////////////////////////////////////
    // finding information on original graph:
    //////////////////////////////////////////////////////////////////////////////////////

    SubHandleGraph extract_subgraph(const HandleGraph &graph, id_t start_id,
                                    id_t end_id, const bool backwards);
                                    
    vector<int> check_handle_as_start_of_path_seq(const string &handle_seq,
                                                  const string &path_seq);

    //////////////////////////////////////////////////////////////////////////////////////
    // creation of new graph:
    //////////////////////////////////////////////////////////////////////////////////////

    VG align_source_to_sink_haplotypes(const unordered_set<string>& source_to_sink_haplotypes);

    void integrate_snarl(SubHandleGraph &old_snarl, const HandleGraph &new_snarl,
                         vector<pair<step_handle_t, step_handle_t>>& embedded_paths,
                         const id_t &source_id, const id_t &sink_id, const bool backwards);

    handle_t overwrite_node_id(const id_t& old_node_id, const id_t& new_node_id);

    bool source_and_sink_handles_map_properly(
        const HandleGraph &graph, const id_t &new_source_id, const id_t &new_sink_id,
        const bool &touching_source, const bool &touching_sink,
        const handle_t &potential_source, const handle_t &potential_sink);

    void force_maximum_handle_size(MutableHandleGraph &graph, const size_t &max_size);

    // moving paths to new graph (new draft functions)
    vector<pair<vector<handle_t>, int> > find_possible_path_starts (const handle_t& leftmost_handle, const handle_t& rightmost_handle, const pair<bool, bool>& path_spans_left_right);

    vector<handle_t> extend_possible_paths(vector<pair<vector<handle_t>, int>> &possible_path_starts, const string &path_str, const handle_t &leftmost_handle, const handle_t &rightmost_handle, const pair<bool, bool> &path_spans_left_right, const pair<id_t, id_t> &main_graph_source_and_sink);

    pair<step_handle_t, step_handle_t> move_path_to_new_snarl(const pair<step_handle_t, step_handle_t> & old_path, const id_t &source, const id_t &sink, const pair<bool, bool> &path_spans_left_right, const bool &path_directed_left_to_right, const pair<id_t, id_t> &main_graph_source_and_sink);

    //////////////////////////////////////////////////////////////////////////////////////
    // format-type switching:
    //////////////////////////////////////////////////////////////////////////////////////
    unordered_set<string> format_handle_haplotypes_to_strings(
        const vector<vector<handle_t>> &haplotype_handle_vectors);


    // -------------------------------- DEBUG CODE BELOW:
    // ------------------------------------

    pair<vector<handle_t>, vector<handle_t>>
    debug_get_sources_and_sinks(const HandleGraph &graph);
};
}
} // namespace vg
