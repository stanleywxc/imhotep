#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <utility>

#include "merge_iterator.hpp"
#include "mmapped_file.hpp"
#include "term_seq_iterator.hpp"

using namespace std;
using namespace imhotep;

template <typename term_t>
void seq(const vector<string>& splits) {
    Shard::packed_table_ptr table(0);
    vector<MergeInput<term_t>> inputs;
    vector<shared_ptr<MMappedFile>> split_files;
    for (string split: splits) {
        split_files.push_back(make_shared<MMappedFile>(split));
        SplitView view(split_files.back()->begin(), split_files.back()->end());
        inputs.push_back(MergeInput<term_t>(SplitIterator<term_t>(view), table, 0));
    }

    MergeIterator<term_t> mit(inputs.begin(), inputs.end());
    MergeIterator<term_t> mend;
    TermSeqIterator<term_t> it(mit, mend);
    TermSeqIterator<term_t> end;
    while (it != end) {
        cout << it->to_string() << endl;
        ++it;
    }
}

int main(int argc, char *argv[])
{
    const string kind(argv[1]);

    vector<string> splits;
    string str;
    while (getline(cin, str) && str.length()) {
        splits.push_back(str);
    }

    if (kind == "int") {
        seq<IntTerm>(splits);
    }
    else if (kind == "string") {
        seq<StringTerm>(splits);
    }
    else {
        cerr << "Say what?" << endl;
        exit(1);
    }
}
