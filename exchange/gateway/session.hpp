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

enum class SessionAction { None, Deliver, Reply, Reject, Logout, Disconnect };

struct SessionResult {
    SessionAction            action{SessionAction::None};
    char                     reply_type{};   // Reply: ce mesaj trimite gateway-ul ('A', '0')
    FIX::SeqNum              ref_seq{};      // Reject: 45
    FIX::SessionRejectReason reason{};       // Reject: 373
    std::string_view         text{};         // Reject / Logout: 58 (gol = fără 58)
};

class Session {
public:
    // GREȘEALĂ (warning -Wreorder): lista de inițializare nu e în ordinea declarării membrilor.
    //   Membrii sunt declarați: ..., logon_timeout_, connected_at_, last_received_, last_sent_,
    //   dar tu scrii last_received_/last_sent_ ÎNAINTEA lui logon_timeout_/connected_at_.
    //   Rescrie lista în ordinea declarării.
    Session(std::string exchange_comp_id, FIX::Timestamp connected_at, FIX::Timestamp logon_timeout) :
        exchange_comp_id_(std::move(exchange_comp_id)),
        last_received_(connected_at),
        last_sent_(connected_at),
        logon_timeout_(logon_timeout),
        connected_at_(connected_at) {}

    SessionResult on_message(const FIX::Message& msg, FIX::Timestamp now) {
        // GREȘEALĂ (nu compilează, peste tot în funcție): namespace-ul se numește FIX::msg_type, nu FIX::MsgType
        //   (vezi common/fix/tags.hpp). Atenție și la nume: e FIX::msg_type::HeartBeat (B mare), nu Heartbeat.
        if (state_ == SessionState::AwaitingLogon) {
            if (msg.type != FIX::MsgType::Logon) {
                return disconnect();
            }
            if (msg.get(FIX::tag::MsgSeqNum) != "1") {
                return logout("Logon: 34 != 1");
            }
            // GREȘEALĂ (nu compilează): tag-ul se numește FIX::tag::EncryptionMethod, nu EncryptMethod.
            if (msg.get(FIX::tag::EncryptMethod) != "0") {
                return logout("Logon: 98 != 0");
            }
            if (msg.get(FIX::tag::HeartBtInt).empty()) {
                return logout("Logon: 108 missing");
            }
            // GREȘEALĂ (logică, mică): parsezi direct în membrul heartbeat_interval_. La "30s" sau "-5" from_chars
            //   scrie totuși 30 / -5 în membru, iar sesiunea rămâne cu o valoare greșită deși Logon-ul a fost respins.
            //   Parsează într-o variabilă locală și copiaz-o în membru abia când tot Logon-ul e valid.
            //   (Stil: ține msg.get(FIX::tag::HeartBtInt) într-un std::string_view local, nu-l apela de 5 ori;
            //   verificarea .empty() de mai sus e redundantă — from_chars pe gol dă deja eroare, dar nu strică.)
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
            // GREȘEALĂ (nu compilează): `state` e getter-ul (funcția state()), membrul este `state_`.
            state = SessionState::Active;
            // GREȘEALĂ (logică): lipsește last_received_ = now; — testul ValidLogonRepliesLogonAndActivates
            //   verifică last_received() == momentul Logon-ului, iar fără asta timer-ul de tăcere (1.5 × HeartBtInt)
            //   s-ar măsura de la conectare, nu de la Logon.
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
                // GREȘEALĂ (nu compilează): std::from_chars întoarce {ptr, ec}, nu {ec, ptr} — legarea
                //   structurată e în ordinea câmpurilor, deci aici ec e de fapt pointer-ul.
                // GREȘEALĂ (de design): nu trebuie să re-parsezi 34 aici. La pasul de mai sus ai verificat deja
                //   34 == std::to_string(expected_incoming_seq_), deci e sigur valid; ramura logout("34 invalid")
                //   nu se poate executa niciodată. Reține secvența ÎNAINTE de ++expected_incoming_seq_
                //   (ex. const FIX::SeqNum seq = expected_incoming_seq_++;) și folosește-o ca ref_seq (45).
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
