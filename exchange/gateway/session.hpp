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
    Session(std::string exchange_comp_id, FIX::Timestamp connected_at, FIX::Timestamp heartbeat_interval):
        exchange_comp_id_(std::move(exchange_comp_id)),
        last_received_(connected_at),
        last_sent_(connected_at),
        heartbeat_interval_(heartbeat_interval) {}

    SessionResult on_message(const FIX::Message& msg, FIX::Timestamp now) {
        if (state_ == SessionState::AwaitingLogon) {
            if (msg.get(FIX::tag::MsgSeqNum) == "") { return {SessionAction::Close, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "missing 34"}; }
            if (msg.get(FIX::tag::SenderCompId) == "") { return {SessionAction::Close, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "missing 49"}; }
            if (msg.get(FIX::tag::TargetCompId) != exchange_comp_id_) { return {SessionAction::Close, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "wrong 56"}; }
            if (msg.type == FIX::msg_type::Logon) {
                if (msg.get(FIX::tag::EncryptionMethod) == "") { state = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "missing 98"}; }
                if (msg.get(FIX::tag::EncryptionMethod) != "0") { state = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "wrong 98"}; }
                if (msg.get(FIX::tag::HeartBtInt) == "") { state = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "missing 108"}; }
                if (msg.get(FIX::tag::HeartBtInt) != "1") { state = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "incorrect 108"}; }
                state = SessionState::Active;
                client_comp_id_ = msg.get(FIX::tag::SenderCompId);
                heartbeat_interval_ = std::stoll(msg.get(FIX::tag::HeartBtInt));
                last_received_ = now;
                expected_incoming_seq_ = 2;
                return {SessionAction::Reply, FIX::msg_type::Logon};
            } else {
                return {SessionAction::Close, 0, 0, FIX::SessionRejectReason::InvalidMsgType, "expected Logon"};
            }
        } else if (state_ == SessionState::Active) {
            if (msg.get(FIX::tag::MsgSeqNum) == "") { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "missing 34"}; }  
            if (msg.get(FIX::tag::MsgSeqNum) != std::to_string(expected_incoming_seq_)) { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "wrong 34"}; }
            if (msg.get(FIX::tag::SenderCompId) == "") { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::RequiredTagMissing, "missing 49"}; }
            if (msg.get(FIX::tag::SenderCompId) != client_comp_id_) { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "wrong 49"}; }
            if (msg.get(FIX::tag::TargetCompId) != exchange_comp_id_) { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "wrong 56"}; }
            if (msg.get(FIX::tag::MsgType) == FIX::msg_type::Logon) { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::InvalidMsgType, "duplicate Logon"}; }
            if (msg.get(FIX::tag::MsgType) == FIX::msg_type::HeartBeat) { last_received_ = now; expected_incoming_seq_++; return {SessionAction::None, 0, 0, FIX::SessionRejectReason::None, ""}; }
            if (msg.get(FIX::tag::MsgType) == FIX::msg_type::Logout) { state_ = SessionState::LoggedOut; return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::None, ""}; }
            if (msg.get(FIX::tag::MsgType) == FIX::msg_type::NewOrderSingle) {
                last_received_ = now;
                expected_incoming_seq_++;
                std::expected<FIX::NewOrderFields, FIX::ConvertError> fields = FIX::decode_new_order(msg);
                if (!fields) {
                    int tmp;
                    auto [ptr, ec] = std::from_chars(msg.get(FIX::tag::MsgSeqNum).data(), msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size(), tmp);
                    if (ec != std::errc{} || ptr != msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size()) {
                        return {SessionAction::Reject, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "invalid MsgSeqNum"};
                    }
                    return {SessionAction::Reply, 0, tmp};
                }
                return {SessionAction::Deliver, 0, 0, FIX::SessionRejectReason::None, ""};
            } else if (msg.get(FIX::tag::MsgType) == FIX::msg_type::OrderCancelRequest) {
                last_received_ = now;
                expected_incoming_seq_++;
                std::expected<FIX::CancelFields, FIX::ConvertError> fields = FIX::decode_cancel(msg);
                if (!fields) {
                    int tmp;
                    auto [ptr, ec] = std::from_chars(msg.get(FIX::tag::MsgSeqNum).data(), msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size(), tmp);
                    if (ec != std::errc{} || ptr != msg.get(FIX::tag::MsgSeqNum).data(), msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size()) {
                        return {SessionAction::Reject, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "invalid MsgSeqNum"};  
                    }
                    return {SessionAction::Reply, 0, tmp};
                }
                return {SessionAction::Deliver, 0, 0, FIX::SessionRejectReason::None, ""};
            } else if (msg.get(FIX::tag::MsgType) == FIX::msg_type::OrderCancelReplaceRequest) {
                last_received_ = now;
                expected_incoming_seq_++;
                std::expected<FIX::ReplaceFields, FIX::ConvertError> fields = FIX::decode_replace(msg);
                if (!fields) {
                    int tmp;
                    auto [ptr, ec] = std::from_chars(msg.get(FIX::tag::MsgSeqNum).data(), msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size(), tmp);
                    if (ec != std::errc{} || ptr != msg.get(FIX::tag::MsgSeqNum).data(), msg.get(FIX::tag::MsgSeqNum).data() + msg.get(FIX::tag::MsgSeqNum).size()) {
                        return {SessionAction::Reject, 0, 0, FIX::SessionRejectReason::ValueIsIncorrect, "invalid MsgSeqNum"};
                    }
                    return {SessionAction::Reject, 0, tmp};
                }
                return {SessionAction::Deliver, 0, 0, FIX::SessionRejectReason::None, ""};
            } else if (msg.get(FIX::tag::MsgType) == FIX::msg_type::Reject) {
                last_received_ = now;
                expected_incoming_seq_++;
                return {SessionAction::None, 0, 0, FIX::SessionRejectReason::None, ""};
            } else {
                last_received_ = now;
                expected_incoming_seq_++;
                return {SessionAction::Reject, 0, std::stoll(msg.get(FIX::tag::MsgSeqNum)), FIX::SessionRejectReason::InvalidMsgType, "unsupported MsgType"};
            }
        }

        return {};
    }
    SessionResult on_timer(FIX::Timestamp now) {
        if (state_ == SessionState::Active) {
            if (now - last_sent_ >= heartbeat_interval_ * 1.5) {
                last_sent_ = now;
                return {SessionAction::Reply, FIX::msg_type::HeartBeat, 0, FIX::SessionRejectReason::None, ""};
            }
            if (now - last_received_ > 1.5 * heartbeat_interval_) {
                state_ = SessionState::LoggedOut;
                return {SessionAction::Logout, 0, 0, FIX::SessionRejectReason::None, "heartbeat timeout"};
            }
        }
        else if (state_ == SessionState::AwaitingLogon) {
            if (now - last_received_ > logon_timeout_) {
                state_ = SessionState::LoggedOut;
                return {SessionAction::Close, 0, 0, FIX::SessionRejectReason::None, "logon timeout"};
            }
        }
        return {SessionAction::None, 0, 0};
    }
    SessionResult reject(FIX::SeqNum ref_seq, FIX::SessionRejectReason reason, std::string_view text) {
        return {};
    }

    
    FIX::SeqNum on_send(FIX::Timestamp now) {
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
    SessionState state_{SessionState::AwaitingLogon};
    std::string exchange_comp_id_;
    std::string client_comp_id_;
    FIX::SeqNum expected_incoming_seq_{1};
    FIX::SeqNum next_outgoing_seq_{1};
    int64_t heartbeat_interval_{0}; // secunde
    int64_t logon_timeout_{1000000};      // secunde
    FIX::Timestamp last_received_{0};     // nanosecunde de la epoch
    FIX::Timestamp last_sent_{0};         // nanosecunde de la epoch
};