/*
SESIUNE FIX — reguli

Stări:
  AwaitingLogon   conexiune nouă, încă fără Logon valid
  Active          logat, mesajele de ordine trec spre engine
  LoggedOut       finală: s-a trimis Logout sau s-a închis; orice mesaj e ignorat,
                  gateway-ul închide conexiunea. Reconectare = Session nou, secvențe de la 1.

Rezultate:
  închis          se taie conexiunea fără să se trimită nimic
  logout          se trimite 35=5 (cu 58=motiv), apoi se închide → LoggedOut
  reject(3, r)    se trimite 35=3 cu 45=secvența mesajului, 373=r, 58=motiv; sesiunea continuă
  răspunde X      se trimite mesajul X; sesiunea continuă
  livrează        mesajul merge la convert → engine
  nimic           nu se trimite nimic

Ordinea verificărilor în Active, pentru orice mesaj primit:
  1. 34 lipsă sau nenumeric           → logout   (fără 34 nu mai poți număra secvențele)
  2. 49 / 56 lipsă sau greșite        → logout   (49 = cel din Logon, 56 = CompID-ul exchange-ului)
  3. 34 != secvența așteptată          → logout   (fără ResendRequest: orice gol sau repetare închide)
  4. abia acum: secvența așteptată++, last_received = now, apoi tratezi tipul

Logon valid = 34=1, 98=0, 108 > 0, 49 prezent, 56 = CompID-ul exchange-ului.

373 SessionRejectReason folosite: 1 tag obligatoriu lipsă, 5 valoare incorectă,
                                  6 format incorect, 11 MsgType invalid.

Fiecare mesaj trimis: secvența de ieșire++ (pornește de la 1), last_sent = now.

┌──────────────────────┬────────────────────────┬──────────────────────────────┬───────────┐
│ mesaj primit         │ AwaitingLogon          │ Active                       │ LoggedOut │
├──────────────────────┼────────────────────────┼──────────────────────────────┼───────────┤
│ A Logon valid        │ răspunde A (98=0,      │ logout (Logon dublu)         │ nimic     │
│                      │ 108 același) → Active  │                              │           │
│ A Logon invalid      │ logout                 │ logout                       │ nimic     │
│ 0 Heartbeat          │ închis                 │ nimic                        │ nimic     │
│ 5 Logout             │ închis                 │ răspunde 5 → LoggedOut,      │ nimic     │
│                      │                        │ închis                       │           │
│ D/F/G valide         │ închis                 │ livrează                     │ nimic     │
│ D/F/G pe care        │ închis                 │ reject(3, 1/5/6)             │ nimic     │
│ convert le respinge  │                        │ (secvența rămâne consumată)  │           │
│ 3 Reject de la client│ închis                 │ nimic (doar log)             │ nimic     │
│ alt tip (1,2,4,8,9,  │ închis                 │ reject(3, 11)                │ nimic     │
│ V, H, ...)           │                        │                              │           │
│ 34 / 49 / 56 lipsă   │ închis                 │ logout                       │ nimic     │
│ sau greșite          │                        │                              │           │
│ secvență greșită     │ (prinsă la Logon       │ logout                       │ nimic     │
│                      │ invalid: 34 != 1)      │                              │           │
├──────────────────────┼────────────────────────┼──────────────────────────────┼───────────┤
│ on_timer(now)        │ fără Logon în          │ now - last_sent >= HeartBtInt│ nimic     │
│                      │ LOGON_TIMEOUT de la    │   → răspunde 0               │           │
│                      │ conectare → închis     │ now - last_received >        │           │
│                      │                        │ 1.5 × HeartBtInt → logout    │           │
└──────────────────────┴────────────────────────┴──────────────────────────────┴───────────┘
*/

// TODO Session — după ce trec testele din SessionMessagesTests.cpp

//
//  1. Hotărăște ce primește constructorul: CompID-ul exchange-ului, LOGON_TIMEOUT,
//     momentul conectării (pentru timeout-ul de logon). Session NU citește ceasul singur.
//
//  2. Membrii: starea, CompID-ul clientului (COPIAT, nu string_view — de ce?),
//     secvența așteptată la intrare, secvența următoare la ieșire, HeartBtInt,
//     last_received, last_sent.
//
//  3. Ce întoarce Session gateway-ului (SessionResult)? Pornește de la coloana „Rezultate”
//     din tabel: ce informații îi trebuie gateway-ului ca să construiască singur fiecare răspuns
//     (tip, secvența de referință pentru 3, motiv 373, text pentru 58)?
//
//  4. Metodele:
//       on_message(const Message&, Timestamp now)  → tabelul de sus, rând cu rând
//       on_timer(Timestamp now)                    → rândul on_timer
//       next_outgoing_seq() / on_sent(Timestamp)   → cine actualizează last_sent?
//       o metodă prin care gateway-ul cere un reject(3) când convert respinge un D/F/G
//         (Session a acceptat deja mesajul; ce secvență pune în 45?)
//
//  5. Teste în tests/SessionTests.cpp, construind FIX::Message direct (fără parser), câte unul
//     pentru fiecare celulă din tabel. Începe cu: Logon valid, D înainte de Logon, secvență sărită.
//
//  Verifică-te: pentru fiecare celulă din tabel poți spune DE CE e acel rezultat și nu altul?

#include <string>
#include "common/types.hpp"
#include "common/messages.hpp"

enum class SessionState { AwaitingLogon, Active, LoggedOut };

enum class SessionAction { None, Deliver, Reply, Reject, Logout, Close };

struct SessionResult {
    SessionAction            action{SessionAction::None};
    char                     reply_type{};   // Reply: ce mesaj trimite gateway-ul ('A', '0')
    FIX::SeqNum              ref_seq{};      // Reject: 45
    FIX::SessionRejectReason reason{};       // Reject: 373
    std::string_view         text{};         // Reject / Logout: 58 (gol = fără 58)
};

class Session {
public:
    Session(std::string exchange_comp_id, FIX::Timestamp connected_at, FIX::Timestamp logon_timeout):
        exchange_comp_id_(std::move(exchange_comp_id)),
        last_received_(connected_at),
        last_sent_(connected_at),
        heartbeat_interval_(logon_timeout) {}

    SessionResult on_message(const FIX::Message& msg, FIX::Timestamp now) {
        if (state == SessionState::AwaitingLogon) {
            // TODO
        } else if (state == SessionState::Active) {
            // TODO
        } 

        return {};
    }
    SessionResult on_timer(FIX::Timestamp now) {

    }
    SessionResult reject(FIX::SeqNum ref_seq, FIX::SessionRejectReason reason, std::string_view text) {

    }

    
    FIX::SeqNum on_send(FIX::Timestamp now) {
    }

    SessionState state() const { return state_; }
    std::string_view exchange_comp_id() const { return exchange_comp_id_; }
    std::string_view client_comp_id() const { return client_comp_id_; }
    FIX::SeqNum expected_incoming_seq() const { return expected_incoming_seq_; }
    FIX::SeqNum next_outgoing_seq() const { return next_outgoing_seq_; }
    int64_t heartbeat_interval() const { return heartbeat_interval_; }
    FIX::Timestamp last_received() const { return last_received_; }
    FIX::Timestamp last_sent() const { return last_sent_; }

private:
    SessionState state_{SessionState::AwaitingLogon};
    std::string exchange_comp_id_;
    std::string client_comp_id_;
    FIX::SeqNum expected_incoming_seq_{1};
    FIX::SeqNum next_outgoing_seq_{1};
    int64_t heartbeat_interval_{0}; // secunde
    FIX::Timestamp last_received_{0};     // nanosecunde de la epoch
    FIX::Timestamp last_sent_{0};         // nanosecunde de la epoch
};