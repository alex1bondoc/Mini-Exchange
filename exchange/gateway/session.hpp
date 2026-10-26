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
#pragma once
#include <string>
#include "common/types.hpp"
#include "common/messages.hpp"
#include "common/fix/parser.hpp"
#include "common/fix/tags.hpp"
#include <string_view>
#include <cstdint>
#include <charconv>
#include <optional>


enum class SessionState { AwaitingLogon, Active, LoggedOut };

// Disconnect = conexiunea se taie FĂRĂ să trimiți nimic; de aceea nu are nevoie de text/reason
enum class SessionAction { None, Deliver, Reply, Reject, Logout, Disconnect };

struct SessionResult {
    SessionAction            action{SessionAction::None};
    char                     reply_type{};   // Reply: ce mesaj trimite gateway-ul ('A', '0')
    FIX::SeqNum              ref_seq{};      // Reject: 45
    // NOTĂ: SessionRejectReason NU are None. Când nu ai reason, nu-l scrie deloc în {...}:
    //       câmpurile omise rămân pe valoarea implicită ({}). Ex: {SessionAction::Deliver} e un rezultat complet.
    //       Pentru Logout cu text, sari peste reason cu {} : {SessionAction::Logout, 0, 0, {}, "text"}.
    FIX::SessionRejectReason reason{};       // Reject: 373
    std::string_view         text{};         // Reject / Logout: 58 (gol = fără 58)
};

class Session {
public:
    // NOTĂ: lista de inițializare trebuie scrisă în ORDINEA declarării membrilor de jos
    //       (state_, exchange_comp_id_, ..., logon_timeout_, connected_at_, last_received_, last_sent_).
    //       Acum last_received_/last_sent_ sunt înaintea lui logon_timeout_/connected_at_ → warning -Wreorder.
    //       Compilatorul îi inițializează oricum în ordinea declarării, nu în cea scrisă aici.
    Session(std::string exchange_comp_id, FIX::Timestamp connected_at, FIX::Timestamp logon_timeout) :
        exchange_comp_id_(std::move(exchange_comp_id)),
        last_received_(connected_at),
        last_sent_(connected_at),
        logon_timeout_(logon_timeout),
        connected_at_(connected_at) {}

    SessionResult on_message(const FIX::Message& msg, FIX::Timestamp now) {
        // NOTĂ: Session = stratul de SESIUNE. Se uită doar la header (35, 34, 49, 56) și la câmpurile de sesiune
        //       (98, 108). NU știe ce e un preț sau o cantitate — asta e treaba lui convert (stratul de deasupra).
        //       Tipul mesajului e în msg.type (char), NU în msg.get(35) — testele nici nu pun fields[35].
        //
        // NOTĂ: fă-ți întâi două funcții private (jos, la private:):
        //         logout(text)   → state_ = LoggedOut și întoarce {Logout, ..., text}
        //         disconnect()   → state_ = LoggedOut și întoarce {Disconnect}
        //       Atunci fiecare eșec e un rând: if (...) return logout("wrong 98");
        //       Un switch (state_) cu câte o funcție privată pe stare (on_awaiting_logon / on_active) ține
        //       on_message scurt.
        //
        // ── AwaitingLogon ──
        //   1. msg.type nu e Logon                       → disconnect()   (PRIMA verificare)
        //   2. 34 trebuie să fie exact "1"                → altfel logout  (text fix → comparație de string_view)
        //   3. 98 trebuie să fie exact "0"                → altfel logout  (gol e și el != "0", deci o singură verificare)
        //   4. 108 număr întreg valid și > 0              → altfel logout
        //        aici chiar parsezi: helper privat (ex. std::optional<int64_t>) cu std::from_chars:
        //          gol → invalid; ec != std::errc{} → invalid; ptr != sfârșit → invalid ("30s"); apoi > 0
        //        (același tipar ca la qty în decode_new_order din convert.hpp). NU std::stoll (vrea std::string, aruncă).
        //   5. 49 nu e gol                                → altfel logout
        //   6. 56 == exchange_comp_id_                    → altfel logout
        //   Totul OK: copiezi 49 în client_comp_id_, salvezi HeartBtInt-ul parsat, expected_incoming_seq_ = 2,
        //             last_received_ = now, state_ = Active, întorci Reply cu 'A'.
        //   Textul de logout trebuie să fie ne-gol (testul InvalidLogonHasText).
        //
        // ── Active ── (ordinea din tabel)
        //   1. 34 lipsă                                   → logout
        //   2. 49 != client_comp_id_ sau 56 != exchange   → logout   (întâi „cine ești”...)
        //   3. 34 != secvența așteptată                   → logout   (...apoi „al câtelea mesaj e”)
        //        comparația cu std::to_string(expected_incoming_seq_) prinde dintr-o lovitură
        //        "abc", "-2", "2x", golul și repetarea — fără parsare.
        //   4. O SINGURĂ DATĂ, înainte de a trata tipul: reține secvența curentă (îți trebuie ca 45),
        //      apoi expected_incoming_seq_++ și last_received_ = now.
        //   5. după msg.type:
        //        Logon (al doilea)  → logout
        //        Heartbeat          → nimic
        //        Logout             → logout (răspunzi cu 5, treci în LoggedOut)
        //        D / F / G          → Deliver. NU decodezi nimic aici! Gateway-ul cheamă convert după Deliver,
        //                             iar dacă convert eșuează, cheamă session.reject(...). (OrderMessagesAreDelivered
        //                             trimite un D fără câmpuri de ordin și așteaptă Deliver.)
        //        Reject (3)         → nimic (dar secvența a fost consumată la pasul 4)
        //        orice altceva      → Reject cu ref_seq = secvența reținută la pasul 4, reason InvalidMsgType
        //
        // ── LoggedOut ── nimic: întorci {}.
        if (state_ == SessionState::AwaitingLogon) {
            if (msg.type != FIX::MsgType::Logon) {
                return disconnect();
            }
            if (msg.get(FIX::tag::MsgSeqNum) != "1") {
                return logout("Logon: 34 != 1");
            }
            if (msg.get(FIX::tag::EncryptMethod) != "0") {
                return logout("Logon: 98 != 0");
            }
            if (msg.get(FIX::tag::HeartBtInt).empty()) {
                return logout("Logon: 108 missing");
            }
            auto [ptr, ec] = std::from_chars(msg.get(FIX::tag::HeartBtInt).data(),
                                            msg.get(FIX::tag::HeartBtInt).data() + msg.get(FIX::tag::HeartBtInt).size(),
                                            heartbeat_interval_);
            if (ec != std::errc{} || ptr != msg.get(FIX::tag::HeartBtInt).data() + msg.get(FIX::tag::HeartBtInt).size() || heartbeat_interval_ <= 0) {
                return logout("Logon: 108 invalid");
            }
            if (msg.get(FIX::tag::SenderCompId).empty()) {
                return logout("Logon: 49 missing");
            }
            if (msg.get(FIX::tag::TargetCompId) != exchange_comp_id_) {
                return logout("Logon: 56 != exchange"); 
            }
            client_comp_id_ = std::string(msg.get(FIX::tag::SenderCompId));
            expected_incoming_seq_ = 2;
            state = SessionState::Active;
            return {SessionAction::Reply, FIX::MsgType::Logon};
        }
        else if (state_ == SessionState::Active) {
            if (msg.get(FIX::tag::MsgSeqNum).empty()) {
                return logout("Active: 34 missing");
            }
            if (msg.get(FIX::tag::SenderCompId) != client_comp_id_ || msg.get(FIX::tag::TargetCompId) != exchange_comp_id_) {
                return logout("Active: 49/56 mismatch");
            }
            if (msg.get(FIX::tag::MsgSeqNum) != std::to_string(expected_incoming_seq_)) {
                return logout("Active: 34 != expected");
            }
            ++expected_incoming_seq_;
            last_received_ = now;
            if (msg.type == FIX::MsgType::Logon) {
                return logout("Active: duplicate Logon");
            }
            else if (msg.type == FIX::MsgType::Heartbeat) {
                return {};
            }
            else if (msg.type == FIX::MsgType::Logout) {
                state_ = SessionState::LoggedOut;
                return {SessionAction::Logout, 0, 0, {}, "Logout received"};
            }
            else if (msg.type == FIX::MsgType::NewOrderSingle ||
                     msg.type == FIX::MsgType::OrderCancelRequest ||
                     msg.type == FIX::MsgType::OrderCancelReplaceRequest) {
                return {SessionAction::Deliver};
            }
            else if (msg.type == FIX::MsgType::Reject) {
                return {};
            }
            else {
                int64_t tmp;
                auto [ec, ptr] = std::from_chars(msg.get(FIX::tag::MsgSeqNum).data(),
                                            msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size(),
                                            tmp);
                if (ec != std::errc{} || ptr != msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size()) {
                    return logout("Active: 34 invalid");
                }
                return {SessionAction::Reject, 0, tmp, FIX::SessionRejectReason::InvalidMsgType, "Invalid MsgType"};
            }
        }
        return {};
    }

    SessionResult on_timer(FIX::Timestamp now) {
        // NOTĂ: unități! Timestamp e în NANOSECUNDE, heartbeat_interval_ e în SECUNDE. Compară doar ns cu ns
        //       (înmulțește cu 1'000'000'000 sau ține un membru separat deja în ns).
        //
        // ── AwaitingLogon ──
        //   now - connected_at_ >= logon_timeout_         → disconnect()  (de la CONECTARE, nu de la last_received_)
        //
        // ── Active ── ordinea contează:
        //   1. ÎNTÂI tăcerea clientului: now - last_received_ > 1.5 × HeartBtInt → logout
        //        (altfel un client mort primește heartbeat-uri la infinit și n-ajungi niciodată la logout)
        //        1.5 * x e double; rămâi în întregi: 2 * (now - last_received_) > 3 * interval_ns
        //   2. apoi: now - last_sent_ >= HeartBtInt (fără 1.5) → Reply cu '0'
        //   NU modifica last_sent_ aici: gateway-ul trimite heartbeat-ul și cheamă on_send(now).
        //
        // ── LoggedOut ── nimic.
        return {};
    }

    SessionResult reject(FIX::SeqNum ref_seq, FIX::SessionRejectReason reason, std::string_view text) {
        // TODO: gateway-ul o cheamă când convert respinge un D/F/G pe care sesiunea l-a livrat deja.
        //       Întorci un rezultat Reject cu ref_seq / reason / text primite. Starea și secvențele NU se schimbă
        //       (secvența a fost deja consumată în on_message).
        return {};
    }

    FIX::SeqNum on_send(FIX::Timestamp now) {
        // TODO: gateway-ul o cheamă pentru FIECARE mesaj pe care îl trimite; valoarea întoarsă merge în 34.
        //       Întorci next_outgoing_seq_ (pornește de la 1), apoi îl crești, și last_sent_ = now.
        return {};
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
    SessionResult logout(std::string_view text) {
        state_ = SessionState::LoggedOut;
        return {SessionAction::Logout, 0, 0, {}, text};
    }
    SessionResult disconnect() {
        state_ = SessionState::LoggedOut;
        return {SessionAction::Disconnect};
    }

    SessionState state_{SessionState::AwaitingLogon};
    std::string exchange_comp_id_;
    std::string client_comp_id_;
    FIX::SeqNum expected_incoming_seq_{1};
    FIX::SeqNum next_outgoing_seq_{1};
    int64_t heartbeat_interval_{0}; // secunde

    FIX::Timestamp logon_timeout_{0};     // nanoseconds
    FIX::Timestamp connected_at_{0};      // nanosecunde de la epoch
    FIX::Timestamp last_received_{0};     // nanosecunde de la epoch
    FIX::Timestamp last_sent_{0};         // nanosecunde de la epoch
};
