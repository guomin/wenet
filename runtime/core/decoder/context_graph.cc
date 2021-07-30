// Copyright 2021 Mobvoi Inc. All Rights Reserved.
// Author: zhendong.peng@mobvoi.com (Zhendong Peng)

#include "decoder/context_graph.h"

#include "fst/determinize.h"

#include "utils/string.h"

namespace wenet {

ContextGraph::ContextGraph() {}

void ContextGraph::BuildContextGraph(
        const std::vector<std::string>& query_contexts,
        const std::shared_ptr<fst::SymbolTable> symbol_table) {
  CHECK(symbol_table != nullptr) << "Symbols table should not be nullptr!";
  symbol_table_ = symbol_table;
  if (query_contexts.empty()) {
    graph_.reset();
    return;
  }

  std::unique_ptr<fst::StdVectorFst> ofst(new fst::StdVectorFst());
  // state 0 is the start state
  int start_state = ofst->AddState();
  // state 1 is the final state
  int final_state = ofst->AddState();
  ofst->SetStart(start_state);
  ofst->SetFinal(final_state, fst::StdArc::Weight::One());

  LOG(INFO) << "Contexts count size: " << query_contexts.size();
  int count = 0;
  for (auto context : query_contexts) {
    if (context.size() > config_.max_context_length) {
      LOG(INFO) << "Skip long context: " << context;
      continue;
    }
    if (++count > config_.max_contexts) break;
    // split context to chars, and reverse for graph search
    // TODO (zhendong.peng): Support bpe based context
    std::vector<std::string> chars;
    SplitUTF8StringToChars(trim(context), &chars);
    std::reverse(chars.begin(), chars.end());
    int prev_state = start_state;
    int next_state = start_state;
    for (size_t i = 0; i < chars.size(); ++i) {
      const std::string& ch = chars[i];
      int word_id = symbol_table_->Find(ch);
      if (word_id == -1) {
        LOG(WARNING) << "Ignore unknown word found during compilation: " << ch;
        break;
      }
      if (i < chars.size() - 1) {
        next_state = ofst->AddState();
      } else {
        next_state = final_state;
      }
      // each state has an escape arc to the start state
      if (i > 0) {
        float escape_score = -config_.context_score * i;
        // the ilabel and the olabel are <blank>, which word id is 0
        ofst->AddArc(prev_state, fst::StdArc(0, 0, escape_score, start_state));
      }
      // acceptor means the ilabel (word_id) equals to olabel
      ofst->AddArc(prev_state, fst::StdArc(word_id, word_id,
                                           config_.context_score, next_state));
      prev_state = next_state;
    }
  }
  std::unique_ptr<fst::StdVectorFst> det_fst(new fst::StdVectorFst());
  fst::Determinize(*ofst, det_fst.get());
  graph_ = std::move(det_fst);
}

}  // namespace wenet
