#pragma once
#include <cassert>
#include <optional>
#include <variant>
#include <vector>
#include <cstddef>
#include "common/messages.hpp"
#include "exchange/engine/book.hpp"
#include "exchange/engine/matcher.hpp"

// =====================================================================================
//  CE FACE ENGINE-UL
//
//  InboundMessage ──► on_message ──► on_new / on_cancel / on_replace ──► EngineOutput
//                                          │
//                                          ├─ validate      (doar citește, NU atinge cartea)
//                                          ├─ match         (matcher.hpp, modifică cartea)
//                                          ├─ emit_fills    (Fill → Trade + 2 ExecReport)
//                                          └─ book.rest     (pune restul în carte)
//
//  Regula de bază: engine-ul NU știe de FIX text, socket-uri sau thread-uri.
//  Primește un struct, scrie structuri în `out`. De asta se testează ca MatcherTests.
//  Mai târziu (SPSC) bucla thread-ului engine va fi: pop → on_message → push(out) → out.clear().
// =====================================================================================

struct EngineOutput {
    std::vector<FIX::ExecReport> reports;
    std::vector<FIX::Trade>      trades;
    void clear() { reports.clear(); trades.clear(); }
};

// Pattern-ul „overloaded” pentru std::visit: moștenește operator() din toate lambdele.
// Dacă adaugi un tip nou în InboundMessage și uiți handler-ul → NU compilează (exact ce vrei).
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

class Engine {
public:
    explicit Engine(std::size_t max_symbols = 16, std::size_t max_fills = 1024) 
    {
        fills_.reserve(max_fills);
        books_.reserve(max_symbols);
        instruments_.reserve(max_symbols);
    }

    // Creează cartea pentru un instrument.
    // Varianta aleasă: books_ e vector indexat DIRECT cu symbol_id (cel mai rapid).
    // Asta impune ca id-urile să vină în ordine 0, 1, 2, ... :
    //     assert(inst.id == static_cast<FIX::SymbolId>(books_.size()));
    //     books_.emplace_back(inst.min_price, inst.max_price, inst.tick_size, capacity);
    // CAPCANĂ: pointerii întorși de book_for se invalidează dacă books_ se realocă.
    // În practică adaugi toate instrumentele la pornire, înainte de primul ordin → OK,
    // plus reserve din constructor.
    void add_instrument(const FIX::Instrument& inst, std::size_t capacity) {
        instruments_.emplace_back(inst);
        books_.emplace_back(OrderBook(inst.id, inst.min_price, inst.max_price, inst.tick_size));

    }

    // Dispecerul — NU are altă logică decât să aleagă handler-ul:
    //     std::visit(overloaded{
    //         [&](const FIX::NewOrder& m)     { on_new(m, out); },
    //         [&](const FIX::CancelOrder& m)  { on_cancel(m, out); },
    //         [&](const FIX::ReplaceOrder& m) { on_replace(m, out); },
    //     }, msg);
    // NU face out.clear() — apelantul (testul sau thread-ul engine) poate procesa
    // mai multe mesaje și abia apoi golește. Nu e treaba engine-ului.
    void on_message(const FIX::InboundMessage& msg, EngineOutput& out) {
        std::visit(overloaded{
            [&](const FIX::NewOrder& m)     { on_new(m, out); },
            [&](const FIX::CancelOrder& m)  { on_cancel(m, out); },
            [&](const FIX::ReplaceOrder& m) { on_replace(m, out); }
        }, msg);
    }

private:
    // ============================== HELPERE ==============================

    // Găsește cartea după id; nullptr dacă nu există. Folosit în on_new / on_cancel / on_replace
    // ca să nu repeți căutarea:
    //     if (id < 0 || id >= static_cast<FIX::SymbolId>(books_.size())) return nullptr;
    //     return &books_[id];
    OrderBook* book_for(FIX::SymbolId id) {
        if (id > books_.size()) { return std::nullptr; }
        return &books_[id];
    }

    // Vei scrie Rejected din ~8 locuri → un singur helper, cu designated initializers
    // (ExecReport are 9 câmpuri, pe poziție e ușor să le încurci):
    //     .type = Rejected, .filled_qty = 0, .leaves_qty = 0, .last_price = 0, .last_qty = 0, .reason = r
    void reject(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                FIX::RejectReason reason, EngineOutput& out) {
        FIX::ExecReport report{.client = client, .order = order, .side = side, .type = FIX::ExecType::Rejected,
                                .filled_qty = 0, .leaves_qty = 0, .last_price = 0,
                                .last_qty = 0, .reason = reason};
        out.reports.emplace_back(report);
    }

    // Raport New: filled = 0, leaves = qty (NU 0!), last_* = 0, reason = None.
    void report_new(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                    FIX::Qty qty, EngineOutput& out) {
        FIX::ExecReport report{.client = client, .order = order, .side = side, .type = FIX::ExecType::New, 
                                .filled_qty = 0, .leaves_qty = qty, .last_price = 0,
                                .last_qty = 0};

        out.reports.emplace_back(report);
    }

    // Raport Canceled: filled = cât s-a executat până acum, leaves = 0, last_* = 0.
    void report_canceled(FIX::ClientId client, FIX::OrderId order, FIX::Side side,
                         FIX::Qty filled, EngineOutput& out) {
        FIX::ExecReport report({.client = client, .order = order, .side = side, .type = FIX::ExecType::Canceled,
                                .filled_qty = filled, .leaves_qty = 0, .last_price = 0, .last_qty = 0});
        out.reports.emplace_back(report);
    }

    // ============================== VALIDARE ==============================

    // Răspunde DOAR la „are ordinul voie să intre?”. NU modifică nimic — de asta primește
    // `const OrderBook&` (compilatorul te oprește dacă încerci să schimbi cartea).
    // Întoarce PRIMA problemă găsită, în ordinea asta:
    //
    //   #  verificare                                                    motiv
    //   1  o.qty <= 0                                                    InvalidQty
    //   2  Market + post_only  (Market vrea să execute, post-only interzice)  InvalidCombination
    //      FOK + post_only     (execuție totală imediată vs. fără execuție)   InvalidCombination
    //      Market + DAY        (DECIZIA TA: reject sau tratat ca FAK — alege,
    //                           documentează și fixează cu un test)            InvalidCombination
    //   3  Limit/StopLimit: price în afara [min, max]                    OutsideBand
    //   4  Limit/StopLimit: price nealiniat la tick                      InvalidPrice
    //   5  Stop/StopLimit:  același lucru pentru stop_price              OutsideBand / InvalidPrice
    //   6  book.contains(o.order)                                        DuplicateOrder
    //   -  altfel                                                        None
    //
    // CAPCANE:
    //   - NU verifica price la Market: convert pune 0, iar dacă min_price > 0,
    //     valid_price(0) == false → ai respinge TOATE ordinele Market.
    //   - valid_price verifică banda + tick-ul împreună. Pentru motive separate verifici
    //     banda întâi (ai nevoie de book.min_price() / book.max_price() — vezi TODO în book.hpp),
    //     apoi valid_price.
    FIX::RejectReason validate(const FIX::NewOrder& o, const OrderBook& book) const {
        if (o.qty <= 0) { return FIX::RejectReason::InvalidQty; }
        if (o.type == FIX::OrderType::Market && o.post_only == true)            { return FIX::RejectReason::InvalidCombination; }
        if (o.tif == FIX::TimeInForce::FOK && o.post_only == true)              { return FIX::RejectReason::InvalidCombination; }
        if (o.type == FIX::OrderType::Market && o.tif == FIX::TimeInForce::DAY) { return FIX::RejectReason::InvalidCombination; }
        
        if (o.type == FIX::OrderType::Limit || o.type == FIX::OrderType::StopLimit) {
            if (o.price < book.get_min_price() || o.price > book.get_max_price())   { return FIX::RejectReason::OutsideBand; }
            if ((o.price - book.get_min_price()) % book.get_tick_size() == 0)       { return FIX::RejectReason::InvalidPrice; }
        }

        if (o.type == FIX::OrderType::Stop || o.type == FIX::OrderType::StopLimit) {
            if (o.stop_price < book.get_min_price() || o.stop_price > book.get_max_price())     { return FIX::RejectReason::OutsideBand; }
            if ((o.stop_price - book.get_min_price()) % book.get_tick_size() == 0)              { return FIX::RejectReason::InvalidPrice; }
        }

        if (book.contains(o.order)) { return FIX::RejectReason::DuplicateOrder; }

        return FIX::RejectReason::None;
    }

    // ============================== ORDIN NOU ==============================

    // Ordinea generală: verificări care NU ating cartea → raport New → match → ce rămâne.
    //
    //   1. book = book_for(o.symbol_id). nullptr → reject(UnknownSymbol), return.
    //
    //   2. if (auto r = validate(o, *book); r != None) → reject(r), return.
    //
    //   3. Stop / StopLimit → deocamdată reject(InvalidCombination), return.
    //      Un Stop NU intră în carte: stă ascuns până când un trade atinge stop_price,
    //      apoi devine Market / Limit. E o funcționalitate separată (stops.hpp), la final.
    //
    //   4. Post-only: vrea să fie MAKER (să adauge lichiditate). Dacă ar executa imediat → reject.
    //        Buy  → există best_ask și best_ask <= o.price  → PostOnlyWouldCross
    //        Sell → există best_bid și best_bid >= o.price  → PostOnlyWouldCross
    //      Exemplu: Buy post-only @100, best_ask 100 → reject; best_ask 101 → OK, intră în carte.
    //      Aici ajung doar Limit (Market + post_only a picat deja în validate).
    //
    //   5. FOK — verificat ÎNAINTE de match, pentru că match modifică cartea și nu se poate anula:
    //        opp = partea opusă lui o.side
    //        lim = Limit  → o.price
    //              Market → capătul benzii: max_price pentru Buy, min_price pentru Sell
    //        book->available(opp, lim) < o.qty → reject(FokNotFilled), return.
    //      Exemplu: asks 30@10, 50@11; Buy FOK 100@11 → available 80 < 100 → reject, carte neatinsă.
    //
    //   6. report_new(o.client, o.order, o.side, o.qty) — clientul află că e acceptat
    //      ÎNAINTE de rapoartele Trade. Ordinea din out.reports contează, testele o verifică.
    //
    //   7. Limita pentru match: Limit → std::optional{o.price}; Market → std::nullopt.
    //        fills_.clear();
    //        executed = match(*book, o.side, lim, o.qty, fills_);
    //
    //   8. emit_fills(...) → Trade + 2 rapoarte pentru fiecare fill.
    //
    //   9. remaining = o.qty - executed:
    //        remaining == 0  → nimic (ultimul raport Trade are deja leaves = 0)
    //        Limit + DAY     → book->rest(o.order, o.client, o.side, o.price, remaining, executed)
    //                          rest întoarce false (pool plin) → report_canceled(filled = executed)
    //        FAK / Market    → report_canceled(filled = executed)
    //        FOK             → nu ajungi aici (pasul 5 garantează execuția totală)
    //
    //      BUG DE REPARAT în book.rest: Order.filled pornește de la 0. Dacă Buy 100 execută 60
    //      și 40 intră în carte, la următorul fill pe el Fill.filled ar fi 10 în loc de 70
    //      → CumQty greșit la client. Soluție: parametru `filled` în rest (vezi TODO în book.hpp).
    //
    //   EXEMPLU COMPLET (fă din el primul test):
    //     Carte: Sell 30@10 (id 1), Sell 50@11 (id 2)
    //     Vine:  Buy Limit DAY 100@11 (id 9)
    //     out.reports:
    //       New    id9  filled 0   leaves 100
    //       Trade  id9  filled 30  leaves 70   last 30@10
    //       Trade  id1  filled 30  leaves 0    last 30@10
    //       Trade  id9  filled 80  leaves 20   last 50@11
    //       Trade  id2  filled 50  leaves 0    last 50@11
    //     out.trades: seq 1 30@10 Buy, seq 2 50@11 Buy
    //     Carte după: Buy 20@11 (id 9, filled 80)
    void on_new(const FIX::NewOrder& o, EngineOutput& out) {
        OrderBook* book = book_for(o.symbol_id);
        if (!book) { return; }
        
    }

    // ============================== FILLS ==============================

    // Pentru fiecare Fill produci 3 lucruri, în ordinea asta:
    //   1. Trade (market data): seq = ++seq_num_, symbol, price = fill.price, qty = fill.qty,
    //      side = partea agresorului, time = momentul primirii ordinului
    //   2. ExecReport agresor:  type Trade, filled = cum, leaves = total_qty - cum,
    //                           last_price = fill.price, last_qty = fill.qty
    //   3. ExecReport resting:  type Trade, client/order/side din fill,
    //                           filled = fill.filled, leaves = fill.leaves, last_* ca mai sus
    //
    // Fill conține starea ordinului RESTING, nu a agresorului → pentru agresor ții un contor:
    //     FIX::Qty cum = start_filled;
    //     for (const Fill& f : fills) { cum += f.qty; ... }
    //
    // De ce primește câmpurile separat și nu `const NewOrder&`: la replace agresorul are deja
    // un filled anterior (cum NU pornește de la 0) și nu există un NewOrder. Așa o singură
    // funcție servește și on_new (start_filled = 0, total_qty = o.qty)
    // și on_replace (start_filled = filled vechi, total_qty = new_qty).
    void emit_fills(FIX::ClientId client, FIX::OrderId order, FIX::Side side, FIX::SymbolId symbol,
                    FIX::Qty total_qty, FIX::Qty start_filled, FIX::Timestamp time,
                    const std::vector<Fill>& fills, EngineOutput& out) {

    }

    // ============================== CANCEL ==============================

    //   1. book_for(o.symbol). nullptr → reject(UnknownSymbol).
    //   2. book->get_order_by_id(o.order). nu există → reject(UnknownOrder).
    //      (se întâmplă și când ordinul tocmai s-a executat complet — „TooLate” în FIX;
    //       pentru engine e același caz: nu mai e în carte. Traducerea e treaba gateway-ului.)
    //   3. COPIAZĂ client, side, filled ÎNAINTE de remove.
    //      DE CE: remove → pool_.release(idx) → orders_[idx] = Order{}. O referință
    //      `const Order&` luată înainte arată numai zerouri după remove. Bug tăcut: nu crapă,
    //      doar trimite date greșite.
    //   4. client copiat != o.client → reject(UnknownOrder).
    //      UnknownOrder, nu un motiv separat: nu-i spui unui client că există ordinul altcuiva.
    //   5. book->remove(o.order).
    //   6. report_canceled(filled = filled-ul copiat).
    void on_cancel(const FIX::CancelOrder& o, EngineOutput& out) {

    }

    // ============================== REPLACE ==============================

    // Fă-l DUPĂ ce merg New + Cancel. Replace = schimbă prețul și/sau cantitatea, același id.
    // new_qty e cantitatea TOTALĂ a ordinului, nu ce rămâne.
    //
    //   filled       = order.filled
    //   leaves_vechi = order.qty
    //   leaves_nou   = o.new_qty - filled
    //
    //   Validări (ca la cancel pentru carte / ordin / client), plus:
    //     !book->valid_price(o.new_price)  → reject(InvalidPrice)
    //     o.new_qty <= filled              → reject(InvalidQty)  (nu poți scădea sub ce s-a executat)
    //
    //   Caz A: același preț și leaves_nou < leaves_vechi
    //     → își PĂSTREAZĂ locul în coadă: book->shrink(idx, leaves_vechi - leaves_nou)
    //       (funcție nouă în book — vezi TODO acolo; `reduce` NU merge, crește filled)
    //     → raport Replaced (filled, leaves_nou)
    //     De ce: dacă ai putea CREȘTE fără să pierzi locul, ai sta primul cu 1 bucată
    //     și apoi ai sări la 1.000.000. Scăderea nu dezavantajează pe nimeni.
    //
    //   Caz B: altfel (preț schimbat SAU qty crescut) → pierde prioritatea:
    //     1. copiezi client, side, filled
    //     2. book->remove(o.order)
    //     3. raport Replaced (filled, leaves_nou)
    //     4. fills_.clear(); executed = match(*book, side, o.new_price, leaves_nou, fills_)
    //        — POATE executa imediat dacă noul preț traversează spread-ul!
    //     5. emit_fills(..., total_qty = o.new_qty, start_filled = filled, ...)
    //     6. rest = leaves_nou - executed > 0 → book->rest(..., rest, filled + executed)
    //        (în carte stau doar ordine DAY, deci restul merge mereu înapoi în carte)
    //        rest întoarce false → report_canceled
    //
    //   Același preț și aceeași cantitate → nimic de schimbat, doar raport Replaced.
    void on_replace(const FIX::ReplaceOrder& o, EngineOutput& out) {

    }

    // ============================== STARE ==============================

    // vector indexat direct cu symbol_id (vezi add_instrument / book_for).
    std::vector<OrderBook>  books_{};
    // Contor pentru Trade.seq. Primul trade are seq 1 (de aici ++seq_num_ în emit_fills).
    FIX::SeqNum             seq_num_{0};
    // Refolosit la fiecare ordin (clear înainte de match) → fără alocări pe calea critică.
    std::vector<Fill>       fills_;
    std::vector<FIX::Instrument&> instruments_;
    // MAI TÂRZIU: stops (ordinele Stop/StopLimit în așteptare), verificate după fiecare trade.
};

// =====================================================================================
//  ORDINEA DE LUCRU (test întâi, apoi codul care îl face să treacă):
//    1. constructor, add_instrument, book_for, on_message, reject
//       → test: simbol necunoscut → Rejected(UnknownSymbol)
//    2. validate → câte un test pentru fiecare rând din tabel
//    3. on_new fără match: Limit care nu traversează → New + ordinul e în carte
//    4. emit_fills + match → EXEMPLUL COMPLET de la on_new
//    5. FAK, Market, FOK (inclusiv: FOK eșuat NU atinge cartea), post-only
//    6. bug-ul filled din rest: execuție parțială → încă un fill pe rest → CumQty corect
//    7. on_cancel: inclusiv client greșit și cancel după fill parțial
//    8. on_replace: caz A (își păstrează locul) și caz B (pierde locul / execută imediat)
// =====================================================================================
