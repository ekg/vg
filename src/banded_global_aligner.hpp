//
//  banded_global_aligner.hpp
//  
//  Contains objects to support Aligner in performing banded global alignment against
//  a graph.
//

#ifndef banded_global_aligner_hpp
#define banded_global_aligner_hpp

#include <stdio.h>
#include <ctype.h>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <list>
#include "vg.pb.h"


using namespace std;

namespace vg {
    
    // This class is the outward-facing interface for banded global graph alignment. It computes
    // optimal alignment of a DNA sequence to a DAG with POA. The alignment will start at any source
    // node in the graph and end at any sink node. It is also restricted to falling within a certain
    // diagonal band from the start node.
    class BandedGlobalAligner {
    public:
        
        /*
         * Initializes banded alignment
         *
         * Args:
         *  alignment                   empty alignment with a sequence (and possibly base qualities)
         *  g                           graph to align to
         *  band_padding                width to expand band by
         *  permissive_banding          expand band to allow all node paths
         *  adjust_for_base_quality     perform base quality adjusted alignment (see QualAdjAligner)
         */
        BandedGlobalAligner(Alignment& alignment, Graph& g,
                            int64_t band_padding, bool permissive_banding = false,
                            bool adjust_for_base_quality = false);
        
        /*
         * Initializes banded multi-alignment, which performs the top scoring alternate alignments in addition
         * to the primary
         *
         * Args:
         *  alignment                   empty alignment with a sequence (and possibly base qualities)
         *  g                           graph to align to
         *  alt_alignments              an empty vector to store alternate alignments in, the first element
         *                              will be a copy of the primary alignment
         *  max_multi_alns              the maximum number of alternate alignments (including the primary)
         *  band_padding                width to expand band by
         *  permissive_banding          expand band to allow all node paths
         *  adjust_for_base_quality     perform base quality adjusted alignment (see QualAdjAligner)
         */
        BandedGlobalAligner(Alignment& alignment, Graph& g,
                            vector<Alignment>& alt_alignments, int64_t max_multi_alns,
                            int64_t band_padding, bool permissive_banding = false,
                            bool adjust_for_base_quality = false);
        
        ~BandedGlobalAligner();
        
        /*
         * Adds path and score to the alignment object given in the constructor. If a multi-alignment
         * vector was also supplied, fills the
         *
         * Args:
         *  score_mat   matrix of match/mismatch scores from Aligner (if performing base quality
         *              adjusted alignment, use QualAdjAligner's adjusted score matrix)
         *  nt_table    table of indices by DNA char from Aligner
         *  gap_open    gap open penalty from Algner (if performing base quality adjusted alignment,
         *              use QualAdjAligner's scaled penalty)
         *  gap_extend  gap extension penalty from Algner (if performing base quality adjusted alignment,
         *              use QualAdjAligner's scaled penalty)
         *
         */
        void align(int8_t* score_mat, int8_t* nt_table, int8_t gap_open, int8_t gap_extend);
        
    private:
        class BAMatrix;
        class BABuilder;
        
        // matrices used in Smith-Waterman-Gotoh alignment algorithm
        enum matrix_t {Match, InsertCol, InsertRow};
        
        // the primary alignment
        Alignment& alignment;
        // vector for alternate alignments, or null if not making any
        vector<Alignment>* alt_alignments;
        int64_t max_multi_alns;
        // perform quality adjusted alignments
        bool adjust_for_base_quality;
        
        vector<BAMatrix*> banded_matrices;
        
        unordered_map<int64_t, int64_t> node_id_to_idx;
        vector<Node*> topological_order;
        unordered_set<Node*> source_nodes;
        unordered_set<Node*> sink_nodes;
        
        // internal constructor that the others funnel into
        BandedGlobalAligner(Alignment& alignment, Graph& g,
                            vector<Alignment>* alt_alignments, int64_t max_multi_alns,
                            int64_t band_padding, bool permissive_banding = false,
                            bool adjust_for_base_quality = false);
        
        // traceback an alignment
        void traceback(int8_t* score_mat, int8_t* nt_table, int8_t gap_open, int8_t gap_extend);
        
        // construction functions
        void graph_edge_lists(Graph& g, bool outgoing_edges, vector<vector<int64_t>>& out_edge_list);
        void topological_sort(Graph& g, vector<vector<int64_t>>& node_edges_out, vector<Node*>& out_topological_order);
        void path_lengths_to_sinks(const string& read, vector<vector<int64_t>>& node_edges_in,
                                   vector<int64_t>& shortest_path_to_sink, vector<int64_t>& longest_path_to_sink);
        void find_banded_paths(const string& read, bool permissive_banding, vector<vector<int64_t>>& node_edges_in,
                               vector<vector<int64_t>>& node_edges_out, int64_t band_padding,
                               vector<bool>& node_masked, vector<pair<int64_t, int64_t>>& band_ends);
        void shortest_seq_paths(vector<vector<int64_t>>& node_edges_out, vector<int64_t>& seq_lens_out);
    };

    // the band from the DP matrix for one node in the graph
    class BandedGlobalAligner::BAMatrix {
    private:

        // these indicate the diagonals in this matrix that the band passes through
        // the bottom index is inclusive
        int64_t top_diag;
        int64_t bottom_diag;
        
        Node* node;
        
        Alignment& alignment;
        
        // length of shortest sequence leading to matrix from a source node
        int64_t cumulative_seq_len;
        
        BAMatrix** seeds;
        int64_t num_seeds;
        
        int8_t* match;
        int8_t* insert_col;
        int8_t* insert_row;
        
        void traceback_internal(BABuilder& builder, int64_t start_row, int64_t start_col, matrix_t start_mat,
                                bool in_lead_gap, int8_t* score_mat, int8_t* nt_table, int8_t gap_open,
                                int8_t gap_extend, bool qual_adjusted);
        
        void print_matrix(matrix_t which_mat);
        void print_band(matrix_t which_mat);
        
    public:
        BAMatrix(Alignment& alignment, Node* node, int64_t top_diag, int64_t bottom_diag,
                 BAMatrix** seeds, int64_t num_seeds, int64_t cumulative_seq_len);
        ~BAMatrix();
        
        // use DP to fill the band with alignment scores
        void fill_matrix(int8_t* score_mat, int8_t* nt_table, int8_t gap_open, int8_t gap_extend, bool qual_adjusted);
        
        // the score in the bottom-right corner of the DP band
        int8_t final_score();
        
        void traceback(BABuilder& builder, int8_t* score_mat, int8_t* nt_table, int8_t gap_open,
                       int8_t gap_extend, bool qual_adjusted);
        
        // debugging functions
        void print_full_matrices();
        void print_rectangularized_bands();
        
        friend class BABuilder;
    };
    
    // translates a traceback path into a Path object and stores it in an Alignment object
    class BandedGlobalAligner::BABuilder {
    private:
        Alignment& alignment;
        
        list<Mapping> node_mappings;
        list<Edit> mapping_edits;
        
        matrix_t matrix_state;
        bool matching;
        Node* current_node;
        int64_t edit_length;
        int64_t edit_read_end_idx;
        
        void finish_current_edit();
        void finish_current_node();
        
    public:
        BABuilder(Alignment& alignment);
        ~BABuilder();
        
        // add next step in traceback
        void update_state(matrix_t matrix, Node* node, int64_t read_idx, int64_t node_idx);
        // call after concluding traceback to finish adding edits to alignment
        void finalize_alignment();
    };
}

#endif /* banded_global_aligner_hpp */

