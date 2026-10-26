#include <gtest/gtest.h>
#include "exchange/gateway/session.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

// Interfața pe care o cer testele (session.hpp):
//
//   enum class SessionAction { None, Deliver, Reply, Reject, Logout, Disconnect };
//
//   struct SessionResult {
//       SessionAction            action{SessionAction::None};
//       char                     reply_type{};   // Reply: ce mesaj trimite gateway-ul ('A', '0')
//       FIX::SeqNum              ref_seq{};      // Reject: 45
//       FIX::SessionRejectReason reason{};       // Reject: 373
//       std::string_view         text{};         // Reject / Logout: 58 (gol = fără 58)
//   };
//
//   Session(std::string exchange_comp_id, FIX::Timestamp connected_at, FIX::Timestamp logon_timeout);
//   SessionResult on_message(const FIX::Message& msg, FIX::Timestamp now);
//   SessionResult on_timer(FIX::Timestamp now);
//   SessionResult reject(FIX::SeqNum ref_seq, FIX::SessionRejectReason reason, std::string_view text);
//   FIX::SeqNum   on_send(FIX::Timestamp now);   // întoarce secvența pentru 34, o crește, last_sent = now
//   + getter-ele: state(), client_comp_id(), expected_incoming_seq(), heartbeat_interval(),
//                 last_received(), last_sent()
//
// Corespondența cu tabelul:
//   închis → Disconnect   logout → Logout   reject(3) → Reject   răspunde X → Reply
//   livrează → Deliver    nimic → None
// După Logout sau Disconnect starea devine LoggedOut.

namespace {

using FIX::SessionRejectReason;

constexpr std::string_view EXCH   = "MYEXCH";
constexpr std::string_view CLIENT = "CLIENT1";

constexpr FIX::Timestamp SEC           = 1'000'000'000;
constexpr FIX::Timestamp T0            = 1'000 * SEC;
constexpr FIX::Timestamp LOGON_TIMEOUT = 10 * SEC;
constexpr int64_t        HB            = 30;   // secunde

using Fields = std::initializer_list<std::pair<int, std::string_view>>;

FIX::Message make(char type, Fields fields) {
    FIX::Message m{};
    m.type = type;
    for (auto [tag, value] : fields) m.fields[tag] = value;
    return m;
}

// mesaj complet de la client: 34, 49, 56 + câmpurile date
FIX::Message msg(char type, std::string_view seq, Fields extra = {}) {
    FIX::Message m = make(type, {{FIX::tag::MsgSeqNum, seq},
                                 {FIX::tag::SenderCompId, CLIENT},
                                 {FIX::tag::TargetCompId, EXCH}});
    for (auto [tag, value] : extra) m.fields[tag] = value;
    return m;
}

FIX::Message logon(std::string_view seq = "1", std::string_view hb = "30") {
    return msg(FIX::msg_type::Logon, seq, {{FIX::tag::EncryptionMethod, "0"}, {FIX::tag::HeartBtInt, hb}});
}

void expect_action(const SessionResult& r, SessionAction a) { EXPECT_EQ(r.action, a); }

class SessionTest : public ::testing::Test {
protected:
    Session session{std::string(EXCH), T0, LOGON_TIMEOUT};

    // Logon valid + trimiterea răspunsului (cum ar face gateway-ul)
    void login(FIX::Timestamp now = T0) {
        ASSERT_EQ(session.on_message(logon(), now).action, SessionAction::Reply);
        session.on_send(now);
    }
};

}  // namespace

// ═════════════════════════════ AwaitingLogon ═════════════════════════════

TEST_F(SessionTest, StartsAwaitingLogon) {
    EXPECT_EQ(session.state(), SessionState::AwaitingLogon);
    EXPECT_EQ(session.expected_incoming_seq(), 1);
}

TEST_F(SessionTest, ValidLogonRepliesLogonAndActivates) {
    const auto r = session.on_message(logon(), T0 + SEC);
    EXPECT_EQ(r.action, SessionAction::Reply);
    EXPECT_EQ(r.reply_type, FIX::msg_type::Logon);
    EXPECT_EQ(session.state(), SessionState::Active);
    EXPECT_EQ(session.client_comp_id(), CLIENT);
    EXPECT_EQ(session.heartbeat_interval(), HB);
    EXPECT_EQ(session.expected_incoming_seq(), 2);
    EXPECT_EQ(session.last_received(), T0 + SEC);
}

// CompID-ul trebuie copiat: bufferul din care vine mesajul se refolosește
TEST_F(SessionTest, ClientCompIdIsCopied) {
    std::string buffer = "CLIENT1";
    FIX::Message m = logon();
    m.fields[FIX::tag::SenderCompId] = buffer;
    ASSERT_EQ(session.on_message(m, T0).action, SessionAction::Reply);

    buffer = "XXXXXXX";
    EXPECT_EQ(session.client_comp_id(), "CLIENT1");
}

TEST_F(SessionTest, LogonWithWrongSeqIsLogout) {
    expect_action(session.on_message(logon("2"), T0), SessionAction::Logout);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

TEST_F(SessionTest, LogonWithoutSeqIsLogout) {
    FIX::Message m = logon();
    m.fields[FIX::tag::MsgSeqNum] = {};
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, LogonWithEncryptionIsLogout) {
    FIX::Message m = logon();
    m.fields[FIX::tag::EncryptionMethod] = "1";
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, LogonWithoutEncryptMethodIsLogout) {
    FIX::Message m = logon();
    m.fields[FIX::tag::EncryptionMethod] = {};
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, LogonWithBadHeartbeatIsLogout) {
    for (std::string_view hb : {"0", "-5", "abc", "", "30s"}) {
        Session s{std::string(EXCH), T0, LOGON_TIMEOUT};
        EXPECT_EQ(s.on_message(logon("1", hb), T0).action, SessionAction::Logout) << "108=" << hb;
        EXPECT_EQ(s.state(), SessionState::LoggedOut) << "108=" << hb;
    }
}

TEST_F(SessionTest, LogonToWrongExchangeIsLogout) {
    FIX::Message m = logon();
    m.fields[FIX::tag::TargetCompId] = "OTHER";
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, LogonWithoutSenderIsLogout) {
    FIX::Message m = logon();
    m.fields[FIX::tag::SenderCompId] = {};
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, InvalidLogonHasText) {
    const auto r = session.on_message(logon("1", "0"), T0);
    EXPECT_EQ(r.action, SessionAction::Logout);
    EXPECT_FALSE(r.text.empty());
}

TEST_F(SessionTest, AnythingButLogonFirstDisconnects) {
    for (char type : {FIX::msg_type::HeartBeat, FIX::msg_type::Logout, FIX::msg_type::NewOrderSingle,
                      FIX::msg_type::OrderCancelRequest, FIX::msg_type::OrderCancelReplaceRequest,
                      FIX::msg_type::Reject, 'V'}) {
        Session s{std::string(EXCH), T0, LOGON_TIMEOUT};
        EXPECT_EQ(s.on_message(msg(type, "1"), T0).action, SessionAction::Disconnect) << "35=" << type;
        EXPECT_EQ(s.state(), SessionState::LoggedOut) << "35=" << type;
    }
}

// ═════════════════════════════ Active: header și secvență ═════════════════════════════

TEST_F(SessionTest, SequentialMessagesAreAccepted) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "2"), T0), SessionAction::Deliver);
    expect_action(session.on_message(msg(FIX::msg_type::HeartBeat, "3"), T0), SessionAction::None);
    expect_action(session.on_message(msg(FIX::msg_type::OrderCancelRequest, "4"), T0), SessionAction::Deliver);
    EXPECT_EQ(session.expected_incoming_seq(), 5);
}

TEST_F(SessionTest, SequenceGapIsLogout) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "2"), T0), SessionAction::Deliver);
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "4"), T0), SessionAction::Logout);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

TEST_F(SessionTest, RepeatedSequenceIsLogout) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "2"), T0), SessionAction::Deliver);
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "2"), T0), SessionAction::Logout);
}

// secvența se verifică ÎNAINTEA tipului: nici heartbeat-ul nu scapă
TEST_F(SessionTest, HeartbeatWithWrongSeqIsLogout) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::HeartBeat, "5"), T0), SessionAction::Logout);
}

TEST_F(SessionTest, MissingSeqIsLogout) {
    login();
    FIX::Message m = msg(FIX::msg_type::NewOrderSingle, "");
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, NonNumericSeqIsLogout) {
    for (std::string_view seq : {"abc", "2x", "-2"}) {
        Session s{std::string(EXCH), T0, LOGON_TIMEOUT};
        ASSERT_EQ(s.on_message(logon(), T0).action, SessionAction::Reply);
        EXPECT_EQ(s.on_message(msg(FIX::msg_type::NewOrderSingle, seq), T0).action, SessionAction::Logout)
            << "34=" << seq;
    }
}

TEST_F(SessionTest, WrongSenderIsLogout) {
    login();
    FIX::Message m = msg(FIX::msg_type::NewOrderSingle, "2");
    m.fields[FIX::tag::SenderCompId] = "CLIENT2";
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, WrongTargetIsLogout) {
    login();
    FIX::Message m = msg(FIX::msg_type::NewOrderSingle, "2");
    m.fields[FIX::tag::TargetCompId] = "OTHER";
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, MissingSenderIsLogout) {
    login();
    FIX::Message m = msg(FIX::msg_type::NewOrderSingle, "2");
    m.fields[FIX::tag::SenderCompId] = {};
    expect_action(session.on_message(m, T0), SessionAction::Logout);
}

TEST_F(SessionTest, AcceptedMessageUpdatesLastReceived) {
    login(T0);
    session.on_message(msg(FIX::msg_type::HeartBeat, "2"), T0 + 5 * SEC);
    EXPECT_EQ(session.last_received(), T0 + 5 * SEC);
}

// ═════════════════════════════ Active: tipuri de mesaj ═════════════════════════════

TEST_F(SessionTest, OrderMessagesAreDelivered) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "2"), T0), SessionAction::Deliver);
    expect_action(session.on_message(msg(FIX::msg_type::OrderCancelRequest, "3"), T0), SessionAction::Deliver);
    expect_action(session.on_message(msg(FIX::msg_type::OrderCancelReplaceRequest, "4"), T0), SessionAction::Deliver);
}

TEST_F(SessionTest, SecondLogonIsLogout) {
    login();
    expect_action(session.on_message(logon("2"), T0), SessionAction::Logout);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

TEST_F(SessionTest, ClientLogoutIsAnsweredWithLogout) {
    login();
    const auto r = session.on_message(msg(FIX::msg_type::Logout, "2"), T0);
    EXPECT_EQ(r.action, SessionAction::Logout);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

TEST_F(SessionTest, ClientRejectIsIgnoredButCounted) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::Reject, "2"), T0), SessionAction::None);
    EXPECT_EQ(session.expected_incoming_seq(), 3);
    EXPECT_EQ(session.state(), SessionState::Active);
}

TEST_F(SessionTest, UnsupportedTypeIsRejected) {
    for (char type : {'1', '2', '4', FIX::msg_type::ExecutionReport, FIX::msg_type::OrderCancelReject, 'V', 'H'}) {
        Session s{std::string(EXCH), T0, LOGON_TIMEOUT};
        ASSERT_EQ(s.on_message(logon(), T0).action, SessionAction::Reply);
        const auto r = s.on_message(msg(type, "2"), T0);
        EXPECT_EQ(r.action, SessionAction::Reject) << "35=" << type;
        EXPECT_EQ(r.ref_seq, 2) << "35=" << type;
        EXPECT_EQ(r.reason, SessionRejectReason::InvalidMsgType) << "35=" << type;
        EXPECT_EQ(s.state(), SessionState::Active) << "35=" << type;
        EXPECT_EQ(s.expected_incoming_seq(), 3) << "35=" << type;
    }
}

// ═════════════════════════════ reject cerut de gateway (convert a eșuat) ═════════════════════════════

TEST_F(SessionTest, GatewayRejectKeepsSessionAndSequence) {
    login();
    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "2"), T0), SessionAction::Deliver);

    const auto r = session.reject(2, SessionRejectReason::RequiredTagMissing, "missing ClOrdID");
    EXPECT_EQ(r.action, SessionAction::Reject);
    EXPECT_EQ(r.ref_seq, 2);
    EXPECT_EQ(r.reason, SessionRejectReason::RequiredTagMissing);
    EXPECT_EQ(r.text, "missing ClOrdID");
    EXPECT_EQ(session.state(), SessionState::Active);

    expect_action(session.on_message(msg(FIX::msg_type::NewOrderSingle, "3"), T0), SessionAction::Deliver);
}

// ═════════════════════════════ secvența de ieșire ═════════════════════════════

TEST_F(SessionTest, OutgoingSeqStartsAtOneAndIncrements) {
    EXPECT_EQ(session.on_send(T0), 1);
    EXPECT_EQ(session.on_send(T0), 2);
    EXPECT_EQ(session.on_send(T0), 3);
}

TEST_F(SessionTest, OnSendUpdatesLastSent) {
    session.on_send(T0 + 3 * SEC);
    EXPECT_EQ(session.last_sent(), T0 + 3 * SEC);
}

// ═════════════════════════════ LoggedOut ═════════════════════════════

TEST_F(SessionTest, LoggedOutIgnoresEverything) {
    login();
    ASSERT_EQ(session.on_message(msg(FIX::msg_type::Logout, "2"), T0).action, SessionAction::Logout);
    for (char type : {FIX::msg_type::Logon, FIX::msg_type::HeartBeat, FIX::msg_type::Logout,
                      FIX::msg_type::NewOrderSingle, 'V'}) {
        EXPECT_EQ(session.on_message(msg(type, "3"), T0).action, SessionAction::None) << "35=" << type;
    }
    EXPECT_EQ(session.on_timer(T0 + 1000 * SEC).action, SessionAction::None);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

// ═════════════════════════════ timer ═════════════════════════════

TEST_F(SessionTest, NoLogonBeforeTimeoutDisconnects) {
    expect_action(session.on_timer(T0 + LOGON_TIMEOUT - 1), SessionAction::None);
    expect_action(session.on_timer(T0 + LOGON_TIMEOUT + SEC), SessionAction::Disconnect);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

TEST_F(SessionTest, LogonInTimeStopsLogonTimeout) {
    login(T0 + SEC);
    EXPECT_NE(session.on_timer(T0 + LOGON_TIMEOUT + SEC).action, SessionAction::Disconnect);
    EXPECT_EQ(session.state(), SessionState::Active);
}

TEST_F(SessionTest, HeartbeatSentAfterSilence) {
    login(T0);
    expect_action(session.on_timer(T0 + HB * SEC - 1), SessionAction::None);

    const auto r = session.on_timer(T0 + HB * SEC);
    EXPECT_EQ(r.action, SessionAction::Reply);
    EXPECT_EQ(r.reply_type, FIX::msg_type::HeartBeat);
}

// heartbeat-ul se măsoară de la ULTIMUL mesaj trimis, nu de la logon
TEST_F(SessionTest, SendingResetsHeartbeatTimer) {
    login(T0);
    session.on_message(msg(FIX::msg_type::HeartBeat, "2"), T0 + 20 * SEC);
    session.on_send(T0 + 20 * SEC);
    expect_action(session.on_timer(T0 + 40 * SEC), SessionAction::None);
}

TEST_F(SessionTest, SilentClientIsLoggedOut) {
    login(T0);
    session.on_send(T0 + 30 * SEC);
    expect_action(session.on_timer(T0 + 45 * SEC), SessionAction::None);   // exact 1.5 × HB: încă nu
    session.on_send(T0 + 45 * SEC);
    expect_action(session.on_timer(T0 + 45 * SEC + 1), SessionAction::Logout);
    EXPECT_EQ(session.state(), SessionState::LoggedOut);
}

TEST_F(SessionTest, ReceivingResetsSilenceTimer) {
    login(T0);
    session.on_message(msg(FIX::msg_type::HeartBeat, "2"), T0 + 40 * SEC);
    session.on_send(T0 + 60 * SEC);
    expect_action(session.on_timer(T0 + 60 * SEC), SessionAction::None);
}
