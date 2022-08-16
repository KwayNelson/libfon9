﻿// \file fon9/fmkt/FmktTypes.hpp
// \author fonwinz@gmail.com
#ifndef __fon9_fmkt_FmktTypes_hpp__
#define __fon9_fmkt_FmktTypes_hpp__
#include "fon9/fmkt/FmktTypes.h"
#include "fon9/Decimal.hpp"

namespace fon9 {

inline f9fmkt_TradingRequestSt StrTo(StrView s, f9fmkt_TradingRequestSt null, const char** endptr = nullptr) {
   return static_cast<f9fmkt_TradingRequestSt>(HexStrTo(s, endptr, null));
}
inline char* ToStrRev(char* pout, f9fmkt_TradingRequestSt v) {
   return HexToStrRev(pout, cast_to_underlying(v));
}

inline f9fmkt_OrderSt StrTo(StrView s, f9fmkt_OrderSt null, const char** endptr = nullptr) {
   return static_cast<f9fmkt_OrderSt>(HexStrTo(s, endptr, null));
}
inline char* ToStrRev(char* pout, f9fmkt_OrderSt v) {
   return HexToStrRev(pout, cast_to_underlying(v));
}

namespace fmkt {
/// \ingroup fmkt
/// 一般價格型別, 最大/最小值: +- 92,233,720,368.54775807
using Pri = Decimal<int64_t, 8>;
/// \ingroup fmkt
/// 一般數量型別.
using Qty = uint64_t;
/// \ingroup fmkt
/// [價+量] 成對出現時使用.
struct PriQty {
   Pri   Pri_{};
   Qty   Qty_{};
};

/// \ingroup fmkt
/// 一般金額型別(=Pri型別): 最大約可表達 +- 92e (92 * 10^6);
using Amt = Pri;
/// \ingroup fmkt
/// 大金額型別, 但小數位縮減成 2 位, 最大約可表達 +- 92,233,720e;
using BigAmt = Decimal<int64_t, 2>;

enum MdReceiverSt : int8_t {
   /// DailyClear 狀態下, 一段時間沒資料.
   DailyClearNoData = -1,
   /// DataIn 狀態下, 一段時間沒資料.
   DataBroken = -2,

   /// 每日清盤 or 系統重啟, 尚未收到任何有效封包.
   DailyClear = 0,
   /// 在 DailyClear 狀態下, 且有收到正確封包後, 一段時間後, 才會進入 DataIn 狀態.
   DataIn = 1,
};
static inline bool MdReceiverSt_IsNoData(MdReceiverSt v) {
   return v < MdReceiverSt::DailyClear;
}

} } // namespaces
#endif//__fon9_fmkt_FmktTypes_hpp__
