#include <vector>
#include "common/messages.hpp"

// TODO: mută EngineOutput în engine.hpp — matcher-ul nu produce rapoarte, doar Fill-uri.
//       Engine-ul e cel care transformă Fill-urile în ExecReport-uri + Trade-uri.
struct EngineOutput {
    std::vector<FIX::ExecReport> reports;
    std::vector<FIX::Trade>      trades;
    void clear() { reports.clear(); trades.clear(); }
};