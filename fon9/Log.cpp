﻿// \file fon9/Log.cpp
// \author fonwinz@gmail.com
#include "fon9/Log.hpp"
#include "fon9/ThreadId.hpp"
#include "fon9/buffer/DcQueueList.hpp"
#include "fon9/buffer/BufferNodeWaiter.hpp"

namespace fon9 {

fon9_API LogLevel            LogLevel_;
fon9_API const LogLevelText  CstrLevelMap[static_cast<int>(LogLevel::Count) + 1] = {
   "[TRACE]",
   "[DEBUG]",
   "[INFO ]",
   "[IMP  ]",
   "[WARN ]",
   "[ERROR]",
   "[FATAL]",
   "[?????]",
};

static void LogWriteToStdout(const LogArgs& /*logArgs*/, BufferList&& buf) {
   if (buf.empty())
      return;
   DcQueueList dcQueue{std::move(buf)};
   for (;;) {
      const auto  mem{dcQueue.PeekCurrBlock()};
      if (mem.first == nullptr)
         break;
      size_t wrsz = fwrite(mem.first, 1, mem.second, stdout);
      int    eno = (wrsz < mem.second ? errno : 0);
      dcQueue.PopConsumed(wrsz);
      if (eno) {
         std::error_condition econd = std::generic_category().default_error_condition(eno);
         dcQueue.ConsumeErr(econd);
         break;
      }
   }
   assert(dcQueue.empty());
}
static FnLogWriter      FnLogWriter_ = &LogWriteToStdout;
static FnLogFlusher     FnLogFlusher_ = nullptr;
static TimeZoneOffset   LogTimeZoneAdjust_;
void (*gWaitLogSystemReady)();

fon9_API void SetLogWriter(FnLogWriter fnLogWriter, TimeZoneOffset tzadj, FnLogFlusher fnLogFlusher) {
   FnLogWriter_ = fnLogWriter ? fnLogWriter : &LogWriteToStdout;
   LogTimeZoneAdjust_ = tzadj;
   FnLogFlusher_ = fnLogFlusher;
}
fon9_API void UnsetLogWriter(FnLogWriter fnLogWriter) {
   if (FnLogWriter_ == fnLogWriter) {
      FnLogWriter_ = &LogWriteToStdout;
      LogTimeZoneAdjust_ = TimeZoneOffset{};
      FnLogFlusher_ = nullptr;
   }
}

fon9_API void LogWrite(const LogArgs& logArgs, BufferList&& buf) {
   FnLogWriter_(logArgs, std::move(buf));
}

fon9_API void AddLogHeader(RevBufferList& rbuf, TimeStamp utctm, LogLevel level) {
   RevPrint(rbuf, ThisThread_.GetThreadIdStr(), GetLevelStr(level));
   RevPut_Date_Time_us(rbuf, utctm + LogTimeZoneAdjust_);
}
fon9_API void LogWrite(LogLevel level, RevBufferList&& rbuf) {
   LogArgs logArgs{level};
   AddLogHeader(rbuf, logArgs.UtcTime_, level);
   FnLogWriter_(logArgs, rbuf.MoveOut());
}

fon9_API void WaitLogFlush() {
   if (FnLogFlusher_)
      FnLogFlusher_();
   else {
      LogArgs        la{LogLevel::Info};
      CountDownLatch waiter{1};
      BufferList     buf;
      buf.push_back(BufferNodeWaiter::Alloc(waiter));
      LogWrite(la, std::move(buf));
      waiter.Wait();
   }
}

}// namespace

//--------------------------------------------------------------------------//
#include "fon9/CTools.h"
fon9_API void fon9_CAPI_CALL fon9_SetLogLevel(fon9_LogLevel lv) {
   if (fon9::cast_to_underlying(lv) < fon9::cast_to_underlying(fon9::LogLevel::Count))
      fon9::LogLevel_ = static_cast<fon9::LogLevel>(lv);
}
fon9_API fon9_LogLevel fon9_CAPI_CALL fon9_GetLogLevel(void) {
   return static_cast<fon9_LogLevel>(fon9::LogLevel_);
}
//--------------------------------------------------------------------------//
