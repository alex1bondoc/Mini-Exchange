/*
ID MAP — traducerea dintre lumea FIX (text) și lumea engine-ului (int64)

De ce există:
  convert  dă  string_view: 49 SenderCompID, 11 ClOrdID, 41 OrigClOrdID, 55 Symbol
  engine   vrea int64:      ClientId,        OrderId,    OrderId,         SymbolId
  IdMap stă între ele, în gateway. Engine-ul nu vede niciodată text, convert nu vede niciodată id-uri interne.

Trei mapări:
  1. CompID   ↔ ClientId   „cine e clientul”      creat la primul Logon, STABIL la reconectare
                                                   (același 49 → același ClientId → ordinele lui din carte rămân ale lui)
  2. Symbol   ↔ SymbolId   „ce instrument”        fix, încărcat la pornire din aceleași Instrument-uri ca engine-ul
  3. (ClientId, ClOrdID) ↔ OrderId                „care ordin”      se schimbă la fiecare D / G / sfârșit de ordin

Reguli:
  - OrderId intern: contor care pornește de la 1 și NU se refolosește niciodată (nici după ce ordinul a murit).
  - ClOrdID e unic doar PER CLIENT: CLIENT1 și CLIENT2 pot folosi amândoi 11=abc → cheia e perechea (ClientId, ClOrdID).
    Consecință de securitate: un client caută DOAR în ordinele lui, deci nu poate anula ordinul altcuiva.
  - Toate string-urile se COPIAZĂ (std::string). string_view-urile din Message pointează în bufferul Framer-ului,
    care se suprascrie la următorul read — exact ca la client_comp_id_ în Session.
  - Engine-ul e sincron: gateway-ul are încă în mână cererea F/G când primește raportul, deci 11/41 pentru
    răspunsul la F/G le ia din cerere. IdMap ține doar ClOrdID-ul CURENT al fiecărui ordin viu.

Ce face gateway-ul cu IdMap la fiecare mesaj de la client (după Session → Deliver și convert OK):
┌──────────────────────────┬─────────────────────────────────────────┬──────────────────────────────────────────┐
│ situație                 │ IdMap                                   │ gateway                                  │
├──────────────────────────┼─────────────────────────────────────────┼──────────────────────────────────────────┤
│ 55 necunoscut (D/F/G)    │ symbol_id → nullopt                     │ D: 8 Rejected (UnknownSymbol), fără engine│
│                          │                                         │ F/G: 9 CancelReject, fără engine          │
│ D, 11 nou                │ add_order → OrderId nou                 │ NewOrder{client, id, ...} → engine        │
│ D, 11 deja folosit       │ add_order → DuplicateClOrdId            │ 8 Rejected (DuplicateOrder), fără engine  │
│ (ordin încă viu)         │ (nu se creează nimic)                   │                                          │
│ F/G, 41 găsit            │ find(client, 41) → OrderId              │ CancelOrder / ReplaceOrder → engine       │
│ F/G, 41 negăsit          │ find → nullopt                          │ 9 CancelReject (UnknownOrder), fără engine│
│ G, 11 nou deja folosit   │ find(client, 11) găsește ceva           │ 9 CancelReject, fără engine               │
└──────────────────────────┴─────────────────────────────────────────┴──────────────────────────────────────────┘

Ce face gateway-ul cu IdMap la fiecare ExecReport de la engine:
┌──────────────────────────┬─────────────────────────────────────────┬──────────────────────────────────────────┐
│ raport                   │ IdMap                                   │ ordinul                                  │
├──────────────────────────┼─────────────────────────────────────────┼──────────────────────────────────────────┤
│ New                      │ nimic                                   │ viu                                      │
│ Trade, leaves_qty > 0    │ nimic                                   │ viu (parțial executat)                   │
│ Trade, leaves_qty == 0   │ remove(order)  — DUPĂ ce ai scris 8-ul  │ mort                                     │
│ Replaced                 │ rename(order, 11 din G)                 │ viu, cu ClOrdID nou                      │
│ Canceled                 │ remove(order)                           │ mort (și restul de FAK / Market)          │
│ Rejected la un D         │ remove(order)                           │ n-a trăit niciodată                      │
│ Rejected la F/G          │ nimic! (ordinul original e tot viu,     │ viu; gateway-ul trimite 9, nu 8          │
│ (UnknownOrder = too late)│ sau l-a șters deja Trade-ul final)      │                                          │
└──────────────────────────┴─────────────────────────────────────────┴──────────────────────────────────────────┘
  Pentru 11 în 8-ul de la un Trade pasiv (ordinul altui client care stătea în carte): cl_ord_id(order).
  De aceea remove se face DUPĂ ce ai scris raportul, altfel nu mai ai ce pune în 11.
*/

// TODO IdMap — teste în tests/IdMapTests.cpp (și adaugă-le în CMakeLists.txt). Începe cu:
//   același CompID → același ClientId; ClOrdID duplicat la același client → eroare;
//   același ClOrdID la doi clienți → OK, OrderId-uri diferite; remove apoi add cu același 11 → OK, OrderId NOU;
//   rename: vechiul 11 nu mai e găsit, noul da; string-ul sursă suprascris → IdMap tot are valoarea veche.

#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cassert>
#include "common/types.hpp"

struct OrderEntry {
    FIX::ClientId client;
    std::string cl_ord_id; 
};

enum class IdMapError { 
    DuplicateClOrdId,
    InvalidClientId,
};

struct transparent_hash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
};

using ClOrdMap = std::unordered_map<std::string, FIX::OrderId, transparent_hash, std::equal_to<>>;

class IdMap {
public:
    // ── clienți ──
    // Întoarce ClientId-ul pentru CompID; dacă e prima dată, îl creează. Chemat de gateway la Logon reușit.
    FIX::ClientId client_id(std::string_view comp_id) {
        auto it = compid_to_client_.find(comp_id);
        if (it != compid_to_client_.end()) {
            return it->second;
        }
        FIX::ClientId new_id = compid_to_client_.size();
        compid_to_client_[std::string(comp_id)] = new_id;
        orders_by_client_.emplace_back();
        return new_id;
    }

    // ── simboluri ──
    // La pornire, pentru fiecare Instrument dat și engine-ului (același id!).
    void add_symbol(const FIX::Instrument& inst) {
        assert(inst.id == static_cast<FIX::SymbolId>(symbols_.size()));
        symbol_to_id_[std::string(inst.symbol.view())] = inst.id;
        symbols_.emplace_back(inst.symbol.view());
    }
    std::optional<FIX::SymbolId> symbol_id(std::string_view symbol) const {
        auto it = symbol_to_id_.find(symbol);
        if (it == symbol_to_id_.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    std::string_view symbol(FIX::SymbolId id) const {
        assert(id >= 0 && id < static_cast<FIX::SymbolId>(symbols_.size()));
        return symbols_[id];
    }

    // ── ordine ──
    // D: creează OrderId nou pentru (client, cl_ord_id). Eroare dacă clientul are deja un ordin VIU cu acest 11.
    std::expected<FIX::OrderId, IdMapError> add_order(FIX::ClientId client, std::string_view cl_ord_id) {
        if (client < 0 || client >= static_cast<FIX::ClientId>(orders_by_client_.size())) {
            return std::unexpected(IdMapError::InvalidClientId);
        }
        if (orders_by_client_[client].find(cl_ord_id) != orders_by_client_[client].end()) {
            return std::unexpected(IdMapError::DuplicateClOrdId);
        }
        FIX::OrderId new_id = next_order_id_++;
        orders_by_client_[client].emplace(cl_ord_id, new_id);
        orders_.emplace(new_id, OrderEntry{client, std::string(cl_ord_id)});
        return new_id;
    }
    // F/G: caută după 41 (și verifică dacă noul 11 de la G e liber). Doar în ordinele ACESTUI client.
    std::optional<FIX::OrderId> find(FIX::ClientId client, std::string_view cl_ord_id) const {
        if (client < 0 || client >= static_cast<FIX::ClientId>(orders_by_client_.size())) {
            return std::nullopt;
        }
        auto it = orders_by_client_[client].find(cl_ord_id);
        if (it == orders_by_client_[client].end()) {
            return std::nullopt;
        }
        return it->second;
    }
    // ExecReport → 11 pentru răspuns.
    std::string_view cl_ord_id(FIX::OrderId order) const noexcept {
        assert(orders_.find(order) != orders_.end());
        return orders_.find(order)->second.cl_ord_id;
    }
    // Replaced: ordinul rămâne cu același OrderId, dar de acum e găsit după noul 11, nu după cel vechi.
    void rename(FIX::OrderId order, std::string_view new_cl_ord_id) noexcept {
        assert(orders_.find(order) != orders_.end());
        auto &entry = orders_.find(order)->second;
        auto &client_orders = orders_by_client_[entry.client];
        client_orders.erase(entry.cl_ord_id);
        client_orders[std::string(new_cl_ord_id)] = order;
        entry.cl_ord_id = std::string(new_cl_ord_id);
    }
    // Ordinul a murit (vezi tabelul de ExecReport). Ambele direcții!
    void remove(FIX::OrderId order) {
        assert(orders_.find(order) != orders_.end());
        auto &entry = orders_.find(order)->second;
        auto &client_orders = orders_by_client_[entry.client];
        client_orders.erase(entry.cl_ord_id);
        orders_.erase(order);
    }

private:
    // ═══════════════════════ MEMBRII — cum să-i alegi ═══════════════════════
    //
    // Regula de bază: o structură pentru FIECARE direcție în care cauți.
    //   text → id   : trebuie hash map (textul nu e un index)
    //   id   → text : depinde de cum arată id-urile (vezi mai jos)
    //
    // CÂND MERGE UN VECTOR INDEXAT (în loc de map)?
    //   Când id-urile sunt DENSE (0, 1, 2, ... fără găuri) și NU se șterg.
    //   Atunci vec[id] e: fără hash, fără alocare la căutare, memorie contiguă (cache-friendly), O(1) garantat.
    //   Dacă id-urile au găuri sau se șterg, vectorul ori crește la infinit, ori are sloturi moarte → map.
    //
    // ── 1. clienți ──
    //   compid_to_client_  (deja ai): CompID → ClientId. Corect.
    //   ClientId-urile le dai TU: următorul = compid_to_client_.size() → 0, 1, 2, ... dense, nu se șterg niciodată
    //   (clientul rămâne cunoscut și după deconectare). Deci direcția inversă poate fi vector:
    //       std::vector<std::string> client_comp_ids_;      // [ClientId] → CompID
    //   Îți trebuie? Doar dacă gateway-ul vrea CompID-ul pentru 56 când trimite un 8 unui client PASIV
    //   (ordinul lui a fost lovit de altcineva). Session-ul lui îl știe deja, deci poți să-l sari deocamdată.
    //
    // ── 2. simboluri ──
    //   symbol_to_id_  (deja ai): Symbol → SymbolId. Corect.
    //   Inversul: DA, vector. Engine-ul tău face assert(inst.id == books_.size()) în add_instrument, deci
    //   SymbolId-urile sunt deja 0, 1, 2, ... dense și nu se șterg:
    //       std::vector<std::string> symbols_;              // [SymbolId] → "AAPL"
    //   În add_symbol: aceeași regulă ca engine-ul (id == symbols_.size()), altfel IdMap și engine-ul nu mai
    //   vorbesc despre același instrument.
    //
    // ── 3. ordine: direcția (ClientId, ClOrdID) → OrderId ──
    //   Varianta A: un singur map cu cheie pereche
    //       std::unordered_map<std::pair<ClientId, std::string>, OrderId, ???>
    //     Problemă: std::hash nu există pentru pair → scrii tu un hash care combină două hash-uri, ȘI vrei
    //     căutare transparentă cu pair<ClientId, string_view> → încă un overload, încă un equal_to custom. Complicat.
    //
    //   Varianta B (recomandată): vector de map-uri, indexat după ClientId
    //       using ClOrdMap = std::unordered_map<std::string, FIX::OrderId, transparent_hash, std::equal_to<>>;
    //       std::vector<ClOrdMap> orders_by_client_;      // [ClientId][ClOrdID] → OrderId
    //     De ce merge vectorul aici: ClientId-urile sunt dense (vezi 1). În client_id(), când creezi un client
    //     nou, faci și orders_by_client_.emplace_back() → cele două rămân mereu de aceeași lungime.
    //     Bonus: căutarea e în doi pași simpli — orders_by_client_[client].find(cl_ord_id) — și folosește direct
    //     transparent_hash-ul pe care îl ai deja. Iar „un client caută doar în ordinele lui” e garantat de structură.
    //
    // ── 4. ordine: direcția OrderId → (ClientId, ClOrdID) ──
    //   Aici NU vector: OrderId-urile cresc toată ziua și nu se refolosesc; ordinele mor (remove).
    //   Un vector indexat după OrderId ar avea milioane de sloturi moarte până seara. → hash map, cu erase la remove:
    //       struct OrderEntry {
    //           FIX::ClientId client;
    //           std::string   cl_ord_id;                     // 11 CURENT (se schimbă la rename)
    //       };
    //       std::unordered_map<FIX::OrderId, OrderEntry> orders_;
    //   De ce ține ȘI client ȘI cl_ord_id? Pentru că e exact „adresa” intrării din direcția 3:
    //       remove(order): e = orders_[order] → orders_by_client_[e.client].erase(e.cl_ord_id) → orders_.erase(order)
    //       rename(order, nou): la fel, ștergi cheia veche din direcția 3, pui cheia nouă, actualizezi e.cl_ord_id
    //   Fără client în OrderEntry n-ai ști în ce map din vector să ștergi.
    //
    // INVARIANT (verifică-l în teste): direcțiile 3 și 4 au mereu aceleași ordine. Orice metodă care
    // modifică una o modifică și pe cealaltă (add_order, rename, remove). Dacă uiți una, ai un ordin „fantomă”.
    //

    std::unordered_map<std::string, FIX::ClientId, transparent_hash, std::equal_to<>> compid_to_client_;
    std::unordered_map<std::string, FIX::SymbolId, transparent_hash, std::equal_to<>> symbol_to_id_;
    std::vector<std::string> symbols_; // [SymbolId] → Symbol
    std::vector<ClOrdMap> orders_by_client_; // [ClientId][ClOrdID] → OrderId
    std::unordered_map<FIX::OrderId, OrderEntry> orders_; // OrderId
    FIX::OrderId next_order_id_{1};
};
