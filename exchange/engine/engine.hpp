// TODO: lipsește #pragma once (ca în celelalte headere)
#include <vector>
#include "common/messages.hpp"
#include "exchange/engine/book.hpp"
#include "exchange/engine/matcher.hpp"

// OK: EngineOutput e acum unde trebuie (am scos TODO-ul vechi de aici).
struct EngineOutput {
    std::vector<FIX::ExecReport> reports;
    std::vector<FIX::Trade>      trades;
    void clear() { reports.clear(); trades.clear(); }
};

class Engine {
public:
    // Creează cartea pentru un instrument.
    //   - OrderBook(inst.min_price, inst.max_price, inst.tick_size, capacity)
    //   - vezi nota de la books_ despre cum o găsești după inst.id
    //   - dacă instrumentul există deja → ignori / assert (decizia ta)
    void add_instrument(const FIX::Instrument& inst, std::size_t capacity) {

    }

    // Punctul de intrare. NU golește `out` — apelantul decide când face clear.
    //   - std::visit pe variant; pentru fiecare tip cheamă on_new / on_cancel / on_replace
    //   - cel mai simplu: o lambda generică + `if constexpr (std::is_same_v<T, FIX::NewOrder>)` ...
    //     sau un struct cu trei operator() (overloaded pattern)
    void on_message(const FIX::InboundMessage& msg, EngineOutput& out) {

    }
private:
    // Fluxul pentru un ordin nou. Fă-l pe pași, în ordinea asta:
    //
    //   1. Găsește cartea după o.symbol_id. Nu există → Rejected(UnknownSymbol), return.
    //
    //   2. reason = validate(o, book). Dacă != None → Rejected(reason), return.
    //      Raport Rejected: filled 0, leaves 0, last_* 0.
    //
    //   3. Stop / StopLimit → (mai târziu) merg în stops, raport New, return.
    //      Până le implementezi: Rejected(InvalidCombination) ca să nu ajungă în carte.
    //
    //   4. Post-only (o.post_only): dacă ar face match imediat → Rejected(PostOnlyWouldCross), return.
    //      „Ar face match”: Buy  → există best_ask și best_ask <= o.price
    //                       Sell → există best_bid și best_bid >= o.price
    //
    //   5. FOK: book.available(partea OPUSĂ, o.price) < o.qty → Rejected(FokNotFilled), return.
    //      (pentru Market FOK nu ai preț: limita e capătul benzii — MAX pentru buy, MIN pentru sell)
    //      Important: verifici ÎNAINTE de match, ca să nu atingi cartea dacă nu se poate executa tot.
    //
    //   6. Raport New: filled 0, leaves = o.qty (NU 0).
    //
    //   7. Limita pentru match: Limit → o.price; Market → nullopt.
    //      fills_.clear(); executed = match(book, o.side, limita, o.qty, fills_);
    //
    //   8. emit_fills(o, fills_, out) → Trade + 2 rapoarte pentru fiecare fill.
    //
    //   9. remaining = o.qty - executed. Dacă remaining > 0, după TIF:
    //        Limit DAY     → book.rest(o.order, o.client, o.side, o.price, remaining)
    //                        dacă rest întoarce false (pool plin) → Canceled pentru rest
    //                        ATENȚIE: rest pune qty = remaining, dar filled rămâne 0 în Order.
    //                        Dacă vrei ca CumQty din rapoartele viitoare ale lui să fie corect,
    //                        Order.filled trebuie setat la executed (ex: rest primește și filled,
    //                        sau setezi după rest prin find + acces non-const).
    //        FAK / Market  → raport Canceled: filled = executed, leaves = 0
    //        FOK           → nu ajungi aici (pasul 5 garantează execuție totală)
    void on_new(const FIX::NewOrder& o, EngineOutput& out) {

    }

    // Cancel:
    //   1. Cartea după o.symbol. Nu există → Rejected(UnknownSymbol) (sau UnknownOrder).
    //   2. find(o.order) în carte. Nu există → Rejected(UnknownOrder)
    //      (la nivel FIX asta devine OrderCancelReject, dar asta e treaba gateway-ului)
    //   3. COPIAZĂ din ordin client, side, filled, qty ÎNAINTE de remove.
    //   4. Verifică că o.client == clientul ordinului (un client nu poate anula ordinul altuia).
    //   5. remove(o.order).
    //   6. Raport Canceled: filled = filled-ul copiat, leaves = 0.
    void on_cancel(const FIX::CancelOrder& o, EngineOutput& out) {

    }

    // Replace — fă-l DUPĂ ce merg New + Cancel. Regula standard:
    //   - găsești ordinul (ca la cancel); nu există → Rejected(UnknownOrder)
    //   - validezi new_price (valid_price) și new_qty (> filled-ul deja executat)
    //   - dacă se schimbă DOAR qty în JOS și prețul e același → își păstrează locul în coadă
    //     (ai nevoie de o funcție în book care scade qty fără să mute ordinul — reduce merge,
    //      dar nu trebuie să atingă filled)
    //   - altfel (preț schimbat sau qty crescut) → pierde prioritatea:
    //     remove + tratat ca ordin nou (poate face match imediat!) cu qty = new_qty - filled
    //   - raport Replaced
    void on_replace(const FIX::ReplaceOrder& o, EngineOutput& out) {

    }

    // Întoarce prima problemă găsită, sau None. Doar verificări, NU atinge cartea.
    //   - o.qty <= 0                                   → InvalidQty
    //   - Limit / StopLimit: price în afara [min, max] → OutsideBand
    //                        price nealiniat la tick   → InvalidPrice
    //     (book.valid_price le face pe amândouă; dacă vrei motive separate, verifici banda întâi)
    //   - Stop / StopLimit: același lucru pentru stop_price
    //   - id deja în carte                             → DuplicateOrder
    //   - combinații invalide                          → InvalidCombination:
    //       Market + post_only     (un Market vrea să execute, post-only interzice asta)
    //       FOK + post_only        (unul cere execuție imediată totală, celălalt o interzice)
    //       Market + DAY           (decizia ta: reject, sau îl tratezi ca FAK — alege și documentează)
    //
    // ATENȚIE: acum funcția nu întoarce nimic → UB. Pune măcar `return FIX::RejectReason::None;`
    FIX::RejectReason validate(const FIX::NewOrder& o, const OrderBook& book) const {

    }

    // Pentru fiecare fill produci 3 lucruri, în ordinea asta:
    //   1. Trade (market data): seq = ++seq_num_, symbol = o.symbol_id, price = fill.price,
    //      qty = fill.qty, side = o.side (partea agresorului), time = o.received_at
    //   2. ExecReport agresor: type Trade, client/order/side din o,
    //      filled = cumulatul agresorului (aduni fill.qty pe parcurs — ține un contor local),
    //      leaves = o.qty - cumulat, last_price = fill.price, last_qty = fill.qty
    //   3. ExecReport resting: type Trade, client/order/side din fill,
    //      filled = fill.filled, leaves = fill.leaves, last_price / last_qty ca mai sus
    //
    // Folosește designated initializers (.type = ..., .leaves_qty = ...) — ExecReport are 9 câmpuri
    // și pe poziție e foarte ușor să le încurci (s-a mai întâmplat în vechiul orderbook.hpp).
    void emit_fills(const FIX::NewOrder& o, const std::vector<Fill>& fills, EngineOutput& out) {

    }

    // PROBLEMĂ: vector<OrderBook> indexat după ce? SymbolId e int64 oarecare.
    //   Variante:
    //     a) unordered_map<SymbolId, OrderBook> — simplu, merge cu orice id
    //     b) vector indexat direct cu symbol_id — cel mai rapid, dar ID-urile trebuie să fie
    //        0, 1, 2, ... (le atribuie exchange-ul la add_instrument). Verifici id < size.
    //   Alege una; ai nevoie și de un helper privat „book_for(symbol) → OrderBook* / nullptr”
    //   ca să nu repeți căutarea în on_new / on_cancel / on_replace.
    std::vector<OrderBook>  books_{};
    // OK: contor pentru Trade.seq. Decide dacă primul trade are seq 0 sau 1
    //     (de obicei 1 — de aici ++seq_num_ în emit_fills).
    FIX::SeqNum             seq_num_{0};
    // OK: refolosit la fiecare ordin (clear înainte de match) → fără alocări pe calea critică.
    std::vector<Fill>       fills_;
    // MAI TÂRZIU: stops (ordinele Stop/StopLimit în așteptare), verificate după fiecare trade.
};
