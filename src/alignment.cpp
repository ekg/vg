#include "alignment.hpp"
#include "stream.hpp"

namespace vg {

int hts_for_each(string& filename, function<void(Alignment&)> lambda) {

    samFile *in = hts_open(filename.c_str(), "r");
    if (in == NULL) return 0;
    bam_hdr_t *hdr = sam_hdr_read(in);
    map<string, string> rg_sample;
    parse_rg_sample_map(hdr->text, rg_sample);
    bam1_t *b = bam_init1();
    while (sam_read1(in, hdr, b) >= 0) {
        Alignment a = bam_to_alignment(b, rg_sample);
        lambda(a);
    }
    bam_destroy1(b);
    bam_hdr_destroy(hdr);
    hts_close(in);
    return 1;

}

int hts_for_each_parallel(string& filename, function<void(Alignment&)> lambda) {

    samFile *in = hts_open(filename.c_str(), "r");
    if (in == NULL) return 0;
    bam_hdr_t *hdr = sam_hdr_read(in);
    map<string, string> rg_sample;
    parse_rg_sample_map(hdr->text, rg_sample);

    int thread_count = get_thread_count();
    vector<bam1_t*> bs; bs.resize(thread_count);
    for (auto& b : bs) {
        b = bam_init1();
    }

    bool more_data = true;
#pragma omp parallel shared(in, hdr, more_data, rg_sample)
    {
        int tid = omp_get_thread_num();
        while (more_data) {
            bam1_t* b = bs[tid];
#pragma omp critical (hts_input)
            if (more_data) {
                more_data = sam_read1(in, hdr, b) >= 0;
            }
            if (more_data) {
                Alignment a = bam_to_alignment(b, rg_sample);
                lambda(a);
            }
        }
    }

    for (auto& b : bs) bam_destroy1(b);
    bam_hdr_destroy(hdr);
    hts_close(in);
    return 1;

}

bam_hdr_t* hts_file_header(string& filename, string& header) {
    samFile *in = hts_open(filename.c_str(), "r");
    if (in == NULL) {
        cerr << "[vg::alignment] could not open " << filename << endl;
        exit(1);
    }
    bam_hdr_t *hdr = sam_hdr_read(in);
    header = hdr->text;
    bam_hdr_destroy(hdr);
    hts_close(in);
    return hdr;
}

bam_hdr_t* hts_string_header(string& header,
                             map<string, int64_t>& path_length,
                             map<string, string>& rg_sample) {
    stringstream hdr;
    hdr << "@HD\tVN:1.5\tSO:unknown\n";
    for (auto& p : path_length) {
        hdr << "@SQ\tSN:" << p.first << "\t" << "LN:" << p.second << "\n";
    }
    for (auto& s : rg_sample) {
        hdr << "@RG\tID:" << s.first << "\t" << "SM:" << s.second << "\n";
    }
    hdr << "@PG\tID:0\tPN:vg\n";
    header = hdr.str();
    string sam = "data:" + header;
    samFile *in = sam_open(sam.c_str(), "r");
    bam_hdr_t *h = sam_hdr_read(in);
    sam_close(in);
    return h;
}

bool get_next_alignment_from_fastq(gzFile fp, char* buffer, size_t len, Alignment& alignment) {

    alignment.Clear();

    // handle name
    if (0!=gzgets(fp,buffer,len)) {
        buffer[strlen(buffer)-1] = '\0';
        string name = buffer;
        name = name.substr(1); // trim off leading @
        // keep trailing /1 /2
        alignment.set_name(name);
    } else { return false; }
    // handle sequence
    if (0!=gzgets(fp,buffer,len)) {
        buffer[strlen(buffer)-1] = '\0';
        alignment.set_sequence(buffer);
    } else {
        cerr << "[vg::alignment.cpp] error: incomplete fastq record" << endl; exit(1);
    }
    // handle "+" sep
    if (0!=gzgets(fp,buffer,len)) {
    } else {
        cerr << "[vg::alignment.cpp] error: incomplete fastq record" << endl; exit(1);
    }
    // handle quality
    if (0!=gzgets(fp,buffer,len)) {
        buffer[strlen(buffer)-1] = '\0';
        string quality = string_quality_char_to_short(buffer);
        //cerr << string_quality_short_to_char(quality) << endl;
        alignment.set_quality(quality);
    } else {
        cerr << "[vg::alignment.cpp] error: incomplete fastq record" << endl; exit(1);
    }

    return true;

}

bool get_next_interleaved_alignment_pair_from_fastq(gzFile fp, char* buffer, size_t len, Alignment& mate1, Alignment& mate2) {
    return get_next_alignment_from_fastq(fp, buffer, len, mate1) && get_next_alignment_from_fastq(fp, buffer, len, mate2);
}

bool get_next_alignment_pair_from_fastqs(gzFile fp1, gzFile fp2, char* buffer, size_t len, Alignment& mate1, Alignment& mate2) {
    return get_next_alignment_from_fastq(fp1, buffer, len, mate1) && get_next_alignment_from_fastq(fp2, buffer, len, mate2);
}


size_t fastq_unpaired_for_each_parallel(string& filename, function<void(Alignment&)> lambda) {
    gzFile fp = (filename != "-") ? gzopen(filename.c_str(), "r") : gzdopen(fileno(stdin), "r");
    size_t len = 2 << 18; // 256k
    size_t nLines = 0;
    int thread_count = get_thread_count();
    //vector<Alignment> alns; alns.resize(thread_count);
    vector<char*> bufs; bufs.resize(thread_count);
    for (auto& buf : bufs) {
        buf = new char[len];
    }
    bool more_data = true;
#pragma omp parallel shared(fp, more_data, bufs)
    {
        int tid = omp_get_thread_num();
        while (more_data) {
            Alignment aln;
            char* buf = bufs[tid];
            bool got_anything = false;
#pragma omp critical (fastq_input)
            {
                if (more_data) {
                    got_anything = more_data = get_next_alignment_from_fastq(fp, buf, len, aln);
                    nLines++;
                }
            }
            if (got_anything) {
                lambda(aln);
            }
        }
    }
    for (auto& buf : bufs) {
        delete[] buf;
    }
    gzclose(fp);
    return nLines;
}

size_t fastq_paired_interleaved_for_each_parallel(string& filename, function<void(Alignment&, Alignment&)> lambda) {
    gzFile fp = (filename != "-") ? gzopen(filename.c_str(), "r") : gzdopen(fileno(stdin), "r");
    size_t len = 2 << 18; // 256k
    size_t nLines = 0;
    int thread_count = get_thread_count();
    //vector<Alignment> alns; alns.resize(thread_count);
    vector<char*> bufs; bufs.resize(thread_count);
    for (auto& buf : bufs) {
        buf = new char[len];
    }
    bool more_data = true;
#pragma omp parallel shared(fp, more_data, bufs)
    {
        int tid = omp_get_thread_num();
        while (more_data) {
            Alignment mate1, mate2;
            char* buf = bufs[tid];
            bool got_anything = false;
#pragma omp critical (fastq_input)
            {
                if (more_data) {
                    got_anything = more_data = get_next_interleaved_alignment_pair_from_fastq(fp, buf, len, mate1, mate2);
                    nLines++;
                }
            }
            if (got_anything) {
                lambda(mate1, mate2);
            }
        }
    }
    for (auto& buf : bufs) {
        delete buf;
    }
    gzclose(fp);
    return nLines;
}

size_t fastq_paired_two_files_for_each_parallel(string& file1, string& file2, function<void(Alignment&, Alignment&)> lambda) {
    gzFile fp1 = (file1 != "-") ? gzopen(file1.c_str(), "r") : gzdopen(fileno(stdin), "r");
    gzFile fp2 = (file2 != "-") ? gzopen(file2.c_str(), "r") : gzdopen(fileno(stdin), "r");
    size_t len = 2 << 18; // 256k
    size_t nLines = 0;
    int thread_count = get_thread_count();
    //vector<Alignment> alns; alns.resize(thread_count);
    vector<char*> bufs; bufs.resize(thread_count);
    for (auto& buf : bufs) {
        buf = new char[len];
    }
    bool more_data = true;
#pragma omp parallel shared(fp1, fp2, more_data, bufs)
    {
        int tid = omp_get_thread_num();
        while (more_data) {
            Alignment mate1, mate2;
            char* buf = bufs[tid];
            bool got_anything = false;
#pragma omp critical (fastq_input)
            {
                if (more_data) {
                    got_anything = more_data = get_next_alignment_pair_from_fastqs(fp1, fp2, buf, len, mate1, mate2);
                    nLines++;
                }
            }
            if (got_anything) {
                lambda(mate1, mate2);
            }
        }
    }
    for (auto& buf : bufs) {
        delete buf;
    }
    gzclose(fp1);
    gzclose(fp2);
    return nLines;
}



size_t fastq_unpaired_for_each(string& filename, function<void(Alignment&)> lambda) {
    gzFile fp = (filename != "-") ? gzopen(filename.c_str(), "r") : gzdopen(fileno(stdin), "r");
    size_t len = 2 << 18; // 256k
    size_t nLines = 0;
    char *buffer = new char[len];
    Alignment alignment;
    while(get_next_alignment_from_fastq(fp, buffer, len, alignment)) {
        lambda(alignment);
        nLines++;
    }
    gzclose(fp);
    delete buffer;
    return nLines;
}

size_t fastq_paired_interleaved_for_each(string& filename, function<void(Alignment&, Alignment&)> lambda) {
    gzFile fp = (filename != "-") ? gzopen(filename.c_str(), "r") : gzdopen(fileno(stdin), "r");
    size_t len = 2 << 18; // 256k
    size_t nLines = 0;
    char *buffer = new char[len];
    Alignment mate1, mate2;
    while(get_next_interleaved_alignment_pair_from_fastq(fp, buffer, len, mate1, mate2)) {
        lambda(mate1, mate2);
        nLines++;
    }
    gzclose(fp);
    delete buffer;
    return nLines;
}

size_t fastq_paired_two_files_for_each(string& file1, string& file2, function<void(Alignment&, Alignment&)> lambda) {
    gzFile fp1 = (file1 != "-") ? gzopen(file1.c_str(), "r") : gzdopen(fileno(stdin), "r");
    gzFile fp2 = (file2 != "-") ? gzopen(file2.c_str(), "r") : gzdopen(fileno(stdin), "r");
    size_t len = 2 << 18; // 256k
    size_t nLines = 0;
    char *buffer = new char[len];
    Alignment mate1, mate2;
    while(get_next_alignment_pair_from_fastqs(fp1, fp2, buffer, len, mate1, mate2)) {
        lambda(mate1, mate2);
        nLines++;
    }
    gzclose(fp1);
    gzclose(fp2);
    delete buffer;
    return nLines;

}

void parse_rg_sample_map(char* hts_header, map<string, string>& rg_sample) {
    string header(hts_header);
    vector<string> header_lines = split_delims(header, "\n");

    for (auto& line : header_lines) {

        // get next line from header, skip if empty
        if ( line.empty() ) { continue; }

        // lines of the header look like:
        // "@RG     ID:-    SM:NA11832      CN:BCM  PL:454"
        //                     ^^^^^^^\ is our sample name
        if (line.find("@RG") == 0) {
            vector<string> rg_parts = split_delims(line, "\t ");
            string name;
            string rg_id;
            for (auto& part : rg_parts) {
                size_t colpos = part.find(":");
                if (colpos != string::npos) {
                    string fieldname = part.substr(0, colpos);
                    if (fieldname == "SM") {
                        name = part.substr(colpos+1);
                    } else if (fieldname == "ID") {
                        rg_id = part.substr(colpos+1);
                    }
                }
            }
            if (name.empty()) {
                cerr << "[vg::alignment] Error: could not find 'SM' in @RG line " << endl << line << endl;
                exit(1);
            }
            if (rg_id.empty()) {
                cerr << "[vg::alignment] Error: could not find 'ID' in @RG line " << endl << line << endl;
                exit(1);
            }
            map<string, string>::iterator s = rg_sample.find(rg_id);
            if (s != rg_sample.end()) {
                if (s->second != name) {
                    cerr << "[vg::alignment] Error: multiple samples (SM) map to the same read group (RG)" << endl
                          << endl
                          << "samples " << name << " and " << s->second << " map to " << rg_id << endl
                          << endl
                          << "It will not be possible to determine what sample an alignment belongs to" << endl
                          << "at runtime." << endl
                          << endl
                          << "To resolve the issue, ensure that RG ids are unique to one sample" << endl
                          << "across all the input files to freebayes." << endl
                          << endl
                          << "See bamaddrg (https://github.com/ekg/bamaddrg) for a method which can" << endl
                          << "add RG tags to alignments." << endl;
                    exit(1);
                }
            }
            // if it's the same sample name and RG combo, no worries
            rg_sample[rg_id] = name;
        }
    }
}

void write_alignments(std::ostream& out, vector<Alignment>& buf) {
    function<Alignment(uint64_t)> lambda =
        [&buf] (uint64_t n) {
        return buf[n];
    };
    stream::write(cout, buf.size(), lambda);
}

short quality_char_to_short(char c) {
    return static_cast<short>(c) - 33;
}

char quality_short_to_char(short i) {
    return static_cast<char>(i + 33);
}

void alignment_quality_short_to_char(Alignment& alignment) {
    alignment.set_quality(string_quality_short_to_char(alignment.quality()));
}

string string_quality_short_to_char(const string& quality) {
    string buffer; buffer.resize(quality.size());
    for (int i = 0; i < quality.size(); ++i) {
        buffer[i] = quality_short_to_char(quality[i]);
    }
    return buffer;
}

void alignment_quality_char_to_short(Alignment& alignment) {
    alignment.set_quality(string_quality_char_to_short(alignment.quality()));
}

string string_quality_char_to_short(const string& quality) {
    string buffer; buffer.resize(quality.size());
    for (int i = 0; i < quality.size(); ++i) {
        buffer[i] = quality_char_to_short(quality[i]);
    }
    return buffer;
}

// remember to clean up with bam_destroy1(b);
bam1_t* alignment_to_bam(const string& sam_header,
                         const Alignment& alignment,
                         const string& refseq,
                         const int32_t refpos,
                         const string& cigar,
                         const string& mateseq,
                         const int32_t matepos,
                         const int32_t tlen) {

    assert(!sam_header.empty());
    string sam_file = "data:" + sam_header + alignment_to_sam(alignment, refseq, refpos, cigar, mateseq, matepos, tlen);
    const char* sam = sam_file.c_str();
    samFile *in = sam_open(sam, "r");
    bam_hdr_t *header = sam_hdr_read(in);
    bam1_t *aln = bam_init1();
    if (sam_read1(in, header, aln) >= 0) {
        bam_hdr_destroy(header);
        sam_close(in); // clean up
        return aln;
    } else {
        cerr << "[vg::alignment] Failure to parse SAM record" << endl
             << sam << endl;
        exit(1);
    }
}

string alignment_to_sam(const Alignment& alignment,
                        const string& refseq,
                        const int32_t refpos,
                        const string& cigar,
                        const string& mateseq,
                        const int32_t matepos,
                        const int32_t tlen) {
    stringstream sam;

    sam << (!alignment.name().empty() ? alignment.name() : "*") << "\t"
        << sam_flag(alignment) << "\t"
        << (refseq.empty() ? "*" : refseq) << "\t"
        << refpos + 1 << "\t"
        //<< (alignment.path().mapping_size() ? refpos + 1 : 0) << "\t" // positions are 1-based in SAM, 0 means unmapped
        << alignment.mapping_quality() << "\t"
        << (alignment.has_path() && alignment.path().mapping_size() ? cigar : "*") << "\t"
        << (mateseq == refseq ? "=" : mateseq) << "\t"
        << matepos + 1 << "\t"
        << tlen << "\t"
        << (!alignment.sequence().empty() ? alignment.sequence() : "*") << "\t";
    // hack much?
    if (!alignment.quality().empty()) {
        const string& quality = alignment.quality();
        for (int i = 0; i < quality.size(); ++i) {
            sam << quality_short_to_char(quality[i]);
        }
    } else {
        sam << "*";
        //sam << string(alignment.sequence().size(), 'I');
    }
    //<< (alignment.has_quality() ? string_quality_short_to_char(alignment.quality()) : string(alignment.sequence().size(), 'I'));
    if (!alignment.read_group().empty()) sam << "\tRG:Z:" << alignment.read_group();
    sam << "\n";
    return sam.str();
}

// act like the path this is against is the reference
// and generate an equivalent cigar
string cigar_against_path(const Alignment& alignment) {
    vector<pair<int, char> > cigar;
    if (!alignment.has_path()) return "";
    const Path& path = alignment.path();
    int l = 0;
    for (const auto& mapping : path.mapping()) {
        mapping_cigar(mapping, cigar);
    }
    return cigar_string(cigar);
}

int32_t sam_flag(const Alignment& alignment) {
    int16_t flag = 0;

    if (alignment.score() == 0) {
        // unmapped
        flag |= BAM_FUNMAP;
    } else {
        // correctly aligned
        flag |= BAM_FPROPER_PAIR;
    }
    // HACKZ -- you can't determine orientation from a single part of the mapping
    // unless the graph is a DAG
    if (alignment.has_path()
        && alignment.path().mapping(0).position().is_reverse()) {
        flag |= BAM_FREVERSE;
    }
    if (alignment.is_secondary()) {
        flag |= BAM_FSECONDARY;
    }
    return flag;
}

Alignment bam_to_alignment(const bam1_t *b, map<string, string>& rg_sample) {

    Alignment alignment;

    // get the sequence and qual
    int32_t lqseq = b->core.l_qseq;
    string sequence; sequence.resize(lqseq);

    uint8_t* qualptr = bam_get_qual(b);
    string quality;//(lqseq, 0);
    quality.assign((char*)qualptr, lqseq);

    // process the sequence into chars
    uint8_t* seqptr = bam_get_seq(b);
    for (int i = 0; i < lqseq; ++i) {
        sequence[i] = "=ACMGRSVTWYHKDBN"[bam_seqi(seqptr, i)];
    }

    // get the read group and sample name
    uint8_t *rgptr = bam_aux_get(b, "RG");
    char* rg = (char*) (rgptr+1);
    //if (!rg_sample
    string sname;
    if (!rg_sample.empty()) {
        sname = rg_sample[string(rg)];
    }

    // Now name the read after the scaffold
    string read_name = bam_get_qname(b);

    // Decide if we are a first read (/1) or second (last) read (/2)
    if(b->core.flag & BAM_FREAD1) {
        read_name += "/1";
    }
    if(b->core.flag & BAM_FREAD2) {
        read_name += "/2";
    }
    
    // If we are marked as both first and last we get /1/2, and if we are marked
    // as neither the scaffold name comes through unchanged as the read name.
    // TODO: produce correct names for intermediate reads on >2 read scaffolds.

    // add features to the alignment
    alignment.set_name(read_name);
    alignment.set_sequence(sequence);
    alignment.set_quality(quality);
    
    // TODO: htslib doesn't wrap this flag for some reason.
    alignment.set_is_secondary(b->core.flag & BAM_FSECONDARY);
    if (sname.size()) {
        alignment.set_sample_name(sname);
        alignment.set_read_group(rg);
    }

    return alignment;
}

int alignment_to_length(const Alignment& a) {
    int l = 0;
    for (const auto& m : a.path().mapping()) {
        l += to_length(m);
    }
    return l;
}

int alignment_from_length(const Alignment& a) {
    int l = 0;
    for (const auto& m : a.path().mapping()) {
        l += from_length(m);
    }
    return l;
}

Alignment strip_from_start(const Alignment& aln, size_t drop) {
    if (!drop) return aln;
    Alignment res;
    res.set_name(aln.name());
    res.set_score(aln.score());
    //cerr << "drop " << drop << " from start" << endl;
    res.set_sequence(aln.sequence().substr(drop));
    if (!aln.has_path()) return res;
    *res.mutable_path() = cut_path(aln.path(), drop).second;
    assert(res.has_path());
    if (alignment_to_length(res) != res.sequence().size()) {
        cerr << "failed!!! drop from start 轰" << endl;
        cerr << pb2json(res) << endl << endl;
        assert(false);
    }
    return res;
}

Alignment strip_from_end(const Alignment& aln, size_t drop) {
    if (!drop) return aln;
    Alignment res;
    res.set_name(aln.name());
    res.set_score(aln.score());
    //cerr << "drop " << drop << " from end" << endl;
    size_t cut_at = aln.sequence().size()-drop;
    //cerr << "Cut at " << cut_at << endl;
    res.set_sequence(aln.sequence().substr(0, cut_at));
    if (!aln.has_path()) return res;
    *res.mutable_path() = cut_path(aln.path(), cut_at).first;
    assert(res.has_path());
    if (alignment_to_length(res) != res.sequence().size()) {
        cerr << "failed!!! drop from end 轰" << endl;
        cerr << pb2json(res) << endl << endl;
        assert(false);
    }
    return res;
}

vector<Alignment> reverse_complement_alignments(const vector<Alignment>& alns, const function<int64_t(int64_t)>& node_length) {
    vector<Alignment> revalns;
    for (auto& aln : alns) {
        revalns.push_back(reverse_complement_alignment(aln, node_length));
    }
    return revalns;
}

Alignment reverse_complement_alignment(const Alignment& aln,
                                       const function<int64_t(int64_t)>& node_length) {
    // We're going to reverse the alignment and all its mappings.
    // TODO: should we/can we do this in place?
    
    Alignment reversed = aln;
    reversed.set_sequence(reverse_complement(aln.sequence()));
    
    if(aln.has_path()) {
        // Now invert the order of the mappings, and for each mapping, flip the
        // is_reverse flag. The edits within mappings also get put in reverse
        // order, get their positions corrected, and get their sequences get
        // reverse complemented.
        *reversed.mutable_path() = reverse_complement_path(aln.path(), node_length);
    }
    
    return reversed;
}

// merge that properly handles long indels
// assumes that alignments should line up end-to-end
Alignment merge_alignments(const vector<Alignment>& alns, bool debug) {

    if (alns.size() == 0) {
        Alignment aln;
        return aln;
    } else if (alns.size() == 1) {
        return alns.front();
    }

    // where possible get node and target lengths
    // to validate after merge
    /*
    map<int64_t, map<size_t, set<const Alignment*> > > node_lengths;
    map<int64_t, map<size_t, set<const Alignment*> > > to_lengths;
    for (auto& aln : alns) {
        auto& path = aln.path();
        // find a mapping that overlaps the whole node
        // note that edits aren't simplified
        // so deletions are intact
        for (size_t i = 0; i < path.mapping_size(); ++i) {
            auto& m = path.mapping(i);
            if (m.position().offset() == 0) {
                // can we see if the next mapping is on the following node
                if (i < path.mapping_size()-1 && path.mapping(i+1).position().offset() == 0
                    && mapping_from_length(path.mapping(i+1)) && mapping_from_length(m)) {
                    // we cover the node, record the to_length and from_length
                    set<const Alignment*>& n = node_lengths[m.position().node_id()][from_length(m)];
                    n.insert(&aln);
                    set<const Alignment*>& t = to_lengths[m.position().node_id()][to_length(m)];
                    t.insert(&aln);
                }
            }
        }
    }
    // verify our input by checking for disagreements
    for (auto& n : node_lengths) {
        auto& node_id = n.first;
        if (n.second.size() > 1) {
            cerr << "disagreement in node lengths for " << node_id << endl;
            for (auto& l : n.second) {
                cerr << "alignments that report length of " << l.first << endl;
                for (auto& a : l.second) {
                    cerr << pb2json(*a) << endl;
                }
            }
        } else {
            //cerr << n.second.begin()->second.size() << " alignments support "
            //     << n.second.begin()->first << " as length for " << node_id << endl;
        }
    }
    */
    
    // parallel merge algorithm
    // for each generation
    // merge 0<-0+1, 1<-2+3, ...
    // until there is only one alignment
    vector<Alignment> last = alns;

    // get the alignments ready for merge
#pragma omp parallel for
    for (size_t i = 0; i < last.size(); ++i) {
        Alignment& aln = last[i];
        //cerr << "on " << i << "th aln" << endl
        //     << pb2json(aln) << endl;
        if (!aln.has_path()) {
            Mapping m;
            Edit* e = m.add_edit();
            e->set_to_length(aln.sequence().size());
            e->set_sequence(aln.sequence());
            *aln.mutable_path()->add_mapping() = m;
        }
    }

    while (last.size() > 1) {
        //cerr << "last size " << last.size() << endl;
        size_t new_count = last.size()/2;
        //cerr << "new count b4 " << new_count << endl;
        new_count += last.size() % 2; // force binary
        //cerr << "New count = " << new_count << endl;
        vector<Alignment> curr; curr.resize(new_count);
#pragma omp parallel for
        for (size_t i = 0; i < curr.size(); ++i) {
            //cerr << "merging " << 2*i << " and " << 2*i+1 << endl;
            // take a pair from the old alignments
            // merge them into this one
            if (2*i+1 < last.size()) {
                auto& a1 = last[2*i];
                auto& a2 = last[2*i+1];
                curr[i] = merge_alignments(a1, a2, debug);
                // check that the merge did the right thing
                /*
                auto& a3 = curr[i];
                for (size_t j = 0; j < a3.path().mapping_size()-1; ++j) {
                    // look up reported node length
                    // and compare to what we saw
                    // skips last mapping
                    auto& m = a3.path().mapping(j);
                    if (from_length(m) == to_length(m)
                        && m.has_position()
                        && m.position().offset()==0
                        && a3.path().mapping(j+1).has_position()
                        && a3.path().mapping(j+1).position().offset()==0) {
                        auto nl = node_lengths.find(m.position().node_id());
                        if (nl != node_lengths.end()) {
                            if (nl->second.find(from_length(m)) == nl->second.end()) {
                                cerr << "node length is not consistent for " << m.position().node_id() << endl;
                                cerr << "expected " << nl->second.begin()->first << endl;
                                cerr << "got " << from_length(m) << endl;
                                cerr << "inputs:" << endl << pb2json(a1) << endl << pb2json(a2)
                                     << endl << "output: " << endl << pb2json(a3) << endl;
                                //exit(1);
                            }
                        }
                    }
                }
                */
            } else {
                auto& a1 = last[2*i];
                //cerr << "no need to merge" << endl;
                curr[i] = a1;
            }
        }
        last = curr;
    }
    Alignment res = last.front();
    *res.mutable_path() = simplify(res.path());
    return res;
}

Alignment merge_alignments(const Alignment& a1, const Alignment& a2, bool debug) {
    //cerr << "overlap is " << overlap << endl;
    // if either doesn't have a path, then treat it like a massive softclip
    if (debug) cerr << "merging alignments " << endl << pb2json(a1) << endl << pb2json(a2) << endl;
    // concatenate them
    Alignment a3;
    a3.set_sequence(a1.sequence() + a2.sequence());
    *a3.mutable_path() = concat_paths(a1.path(), a2.path());
    if (debug) cerr << "merged alignments, result is " << endl << pb2json(a3) << endl;
    return a3;
}

void translate_nodes(Alignment& a, const map<id_t, pair<id_t, bool> >& ids, const std::function<size_t(int64_t)>& node_length) {
    Path* path = a.mutable_path();
    for(size_t i = 0; i < path->mapping_size(); i++) {
        // Grab each mapping (includes its position)
        Mapping* mapping = path->mutable_mapping(i);
        auto pos = mapping->position();
        auto oldp = ids.find(pos.node_id());
        if (oldp != ids.end()) {
            auto& old = oldp->second;
            cerr << "translating " << pos.node_id() << " -> " << old.first << (old.second?"-":"+") << endl;
            mapping->mutable_position()->set_node_id(old.first);
            if (old.second) {
                cerr << "flipping mapping " << pb2json(*mapping) << " -> ";
                //mapping->mutable_position()->set_is_reverse(old.second);
                *mapping = reverse_complement_mapping(*mapping, node_length);
                cerr << pb2json(*mapping) << endl;
            }
        }
    }
}

void flip_nodes(Alignment& a, const set<int64_t>& ids, const std::function<size_t(int64_t)>& node_length) {
    Path* path = a.mutable_path();
    for(size_t i = 0; i < path->mapping_size(); i++) {
        // Grab each mapping (includes its position)
        Mapping* mapping = path->mutable_mapping(i);
        if(ids.count(mapping->position().node_id())) {
            // We need to flip this mapping
            *mapping = reverse_complement_mapping(*mapping, node_length);
        } 
    }
}

int softclip_start(Alignment& alignment) {
    if (alignment.mutable_path()->mapping_size() > 0) {
        Path* path = alignment.mutable_path();
        Mapping* first_mapping = path->mutable_mapping(0);
        Edit* first_edit = first_mapping->mutable_edit(0);
        if (first_edit->from_length() == 0 && first_edit->to_length() > 0) {
            return first_edit->to_length();
        }
    }
    return 0;
}

int softclip_end(Alignment& alignment) {
    if (alignment.mutable_path()->mapping_size() > 0) {
        Path* path = alignment.mutable_path();
        Mapping* last_mapping = path->mutable_mapping(path->mapping_size()-1);
        Edit* last_edit = last_mapping->mutable_edit(last_mapping->edit_size()-1);
        if (last_edit->from_length() == 0 && last_edit->to_length() > 0) {
            return last_edit->to_length();
        }
    }
    return 0;
}

size_t to_length_after_pos(const Alignment& aln, const Position& pos) {
    return path_to_length(cut_path(aln.path(), pos).second);
}

size_t from_length_after_pos(const Alignment& aln, const Position& pos) {
    return path_from_length(cut_path(aln.path(), pos).second);
}

size_t to_length_before_pos(const Alignment& aln, const Position& pos) {
    return path_to_length(cut_path(aln.path(), pos).first);
}

size_t from_length_before_pos(const Alignment& aln, const Position& pos) {
    return path_from_length(cut_path(aln.path(), pos).first);
}

const string hash_alignment(const Alignment& aln) {
    string data;
    aln.SerializeToString(&data);
    return sha1sum(data);
}

Alignment simplify(const Alignment& a) {
    auto aln = a;
    *aln.mutable_path() = simplify(aln.path());
    return aln;
}

void write_alignment_to_file(const Alignment& aln, const string& filename) {
    ofstream out(filename);
    vector<Alignment> alnz = { aln };
    stream::write_buffered(out, alnz, 1);
    out.close();
}

}
