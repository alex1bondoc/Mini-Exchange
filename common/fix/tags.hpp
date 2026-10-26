#pragma once

namespace FIX{
    constexpr char SOH = '\x01';
    namespace tag{
        constexpr int BeginString           = 8;
        constexpr int BodyLength            = 9;
        constexpr int CheckSum              = 10;
        constexpr int MessageType           = 35;
        constexpr int SenderCompId          = 49;
        constexpr int TargetCompId          = 56;
        constexpr int MsgSeqNum             = 34;
        constexpr int SendingTime           = 52;
        constexpr int ClOrdId               = 11;
        constexpr int OrigClOrdId           = 41;
        constexpr int Symbol                = 55;
        constexpr int Side                  = 54; // 1 buy 2 sell
        constexpr int OrderQty              = 38;
        constexpr int OrderType             = 40; // 1 market, 2 limit, 3 stop, 4 stoplimit
        constexpr int Price                 = 44;
        constexpr int StopPx                = 99;
        constexpr int TimeInForce           = 59; // 0 day, 3 fak, 4 fok
        constexpr int ExecInst              = 18;
        constexpr int OrderId               = 37;
        constexpr int ExecId                = 17;
        constexpr int ExecType              = 150; // 0 new, 4 canceled, 8 rejected, F trade
        constexpr int OrdStatus             = 39;  // 0 new, 1 partialfill, 2 fill, 4 canceled, 8 Reject
        constexpr int CumQty                = 14;
        constexpr int LeavesQty             = 151;
        constexpr int LastPx                = 31;
        constexpr int LastQty               = 32;
        constexpr int Text                  = 58;
        constexpr int HeartBtInt            = 108;
        constexpr int CxlRejResponseTo      = 434;
        constexpr int CxlRejReason          = 102;
        constexpr int EncryptionMethod      = 98;
        constexpr int RefSeqNum             = 45;
        constexpr int SessionRejectReason   = 373;
    }
    namespace msg_type{
        constexpr char HeartBeat                    = '0';
        constexpr char Logon                        = 'A';
        constexpr char Logout                       = '5';
        constexpr char Reject                       = '3';
        constexpr char NewOrderSingle               = 'D';
        constexpr char OrderCancelRequest           = 'F';
        constexpr char ExecutionReport              = '8';
        constexpr char OrderCancelReplaceRequest    = 'G';
        constexpr char OrderCancelReject            = '9';
    }
}
