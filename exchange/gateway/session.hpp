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
    Session(std::string exchange_comp_id, FIX::Timestamp connected_at, FIX::Timestamp logon_timeout) :
        exchange_comp_id_(std::move(exchange_comp_id)),
        logon_timeout_(logon_timeout),
        connected_at_(connected_at),
        last_received_(connected_at),
        last_sent_(connected_at) {}

    SessionResult on_message(const FIX::Message& msg, FIX::Timestamp now) {
        if (state_ == SessionState::AwaitingLogon) {
            if (msg.type != FIX::msg_type::Logon) {
                return disconnect();
            }
            if (msg.get(FIX::tag::MsgSeqNum) != "1") {
                return logout("Logon: 34 != 1");
            }
            if (msg.get(FIX::tag::EncryptionMethod) != "0") {
                return logout("Logon: 98 != 0");
            }
            if (msg.get(FIX::tag::HeartBtInt).empty()) {
                return logout("Logon: 108 missing");
            }
            int64_t heartbeat_interval{};
            auto [ptr, ec] = std::from_chars(msg.get(FIX::tag::HeartBtInt).data(),
                                            msg.get(FIX::tag::HeartBtInt).data() + msg.get(FIX::tag::HeartBtInt).size(),
                                            heartbeat_interval);
            if (ec != std::errc{} || ptr != msg.get(FIX::tag::HeartBtInt).data() + msg.get(FIX::tag::HeartBtInt).size() || heartbeat_interval <= 0) {
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
            heartbeat_interval_ = heartbeat_interval;
            last_received_ = now;

            state_ = SessionState::Active;
            return {SessionAction::Reply, FIX::msg_type::Logon};
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
            FIX::SeqNum received_seq = expected_incoming_seq_++;
            last_received_ = now;
            if (msg.type == FIX::msg_type::Logon) {
                return logout("Active: duplicate Logon");
            }
            else if (msg.type == FIX::msg_type::HeartBeat) {
                return {};
            }
            else if (msg.type == FIX::msg_type::Logout) {
                return logout("Active: Logout received");
            }
            else if (msg.type == FIX::msg_type::NewOrderSingle ||
                     msg.type == FIX::msg_type::OrderCancelRequest ||
                     msg.type == FIX::msg_type::OrderCancelReplaceRequest) {
                return {SessionAction::Deliver};
            }
            else if (msg.type == FIX::msg_type::Reject) {
                return {};
            }
            else {
                return {SessionAction::Reject, 0, received_seq, FIX::SessionRejectReason::InvalidMsgType, "Invalid MsgType"};
            }
        }
        return {};
    }

    SessionResult on_timer(FIX::Timestamp now) {
        if (state_ == SessionState::AwaitingLogon) {
            if (now - connected_at_ >= logon_timeout_) {
                return disconnect();
            }
        }
        else if (state_ == SessionState::Active) {
            if (2 * (now - last_received_) > 3 * heartbeat_interval_ * 1'000'000'000) {
                return logout("Active: heartbeat timeout");
            }
            if (now - last_sent_ >= heartbeat_interval_ * 1'000'000'000) {
                return {SessionAction::Reply, FIX::msg_type::HeartBeat};
            }
        }
        return {};
    }

    SessionResult reject(FIX::SeqNum ref_seq, FIX::SessionRejectReason reason, std::string_view text) {
        return {SessionAction::Reject, 0, ref_seq, reason, text};
    }

    FIX::SeqNum on_send(FIX::Timestamp now) {
        last_sent_ = now;
        return next_outgoing_seq_++;
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
