﻿/// \file fon9/io/Device.cpp
/// \author fonwinz@gmail.com
#include "fon9/io/Device.hpp"
#include "fon9/TimeStamp.hpp"
#include "fon9/DefaultThreadPool.hpp"

namespace fon9 { namespace io {

Device::~Device() {
   this->State_ = State::Destructing;
   this->CommonTimer_.DisposeAndWait(); // 如果沒使用 DeviceSP, 可能沒執行到 FreeDevice()? 以防萬一, 再呼叫一次停止.
   this->Session_->OnDevice_Destructing(*this);
   if (this->Manager_)
      this->Manager_->OnDevice_Destructing(*this);
}
void Device::FreeDevice() {
   this->CommonTimer_.DisposeAndWait();
   delete this;
}

void Device::Initialize() {
   assert(this->State_ == State::Initializing);
   this->Session_->OnDevice_Initialized(*this);
   if (this->Manager_)
      this->Manager_->OnDevice_Initialized(*this);
   this->State_ = State::Initialized;
}

void Device::MakeCallForWork() {
   DeviceSP pthis{this};
   GetDefaultThreadPool().EmplaceMessage([pthis]() {
      pthis->OpQueue_.TakeCall();
   });
}

void Device::OpThr_Open(Device& dev, std::string cfgstr) {
   State st = dev.State_;
   if (st >= State::Disposing)
      return;
   if (!dev.Session_->OnDevice_BeforeOpen(dev, cfgstr))
      return;
   if (st <= State::Initialized || !cfgstr.empty()) // first open || force reopen, use new config(cfgstr)
      return dev.OpImpl_Open(std::move(cfgstr));
   // 沒有 cfgstr, 檢查現在狀態是否允許 reopen.
   if (st == State::Listening ||
       st == State::LinkReady ||
       st == State::WaitingLinkIn ||
       st == State::ConfigError)
      return;
   dev.OpImpl_Reopen();
}

void Device::OpThr_Close(Device& dev, std::string cause) {
   if (dev.State_ < State::Closing)
      dev.OpImpl_Close(std::move(cause));
}
void Device::OpThr_CheckLingerClose(Device& dev, std::string cause) {
   if (dev.State_ == State::Lingering && dev.IsSendBufferEmpty())
      dev.OpImpl_Close(std::move(cause));
}
void Device::OpThr_LingerClose(Device& dev, std::string cause) {
   if (dev.State_ == State::LinkReady) {
      dev.OpImpl_SetState(State::Lingering, &cause);
      OpThr_CheckLingerClose(dev, std::move(cause));
   }
   else {
      if (dev.State_ < State::Lingering)
         dev.OpImpl_Close(std::move(cause));
   }
}

void Device::OpImpl_Dispose(std::string cause) {
   (void)cause;
}
void Device::OpThr_Dispose(Device& dev, std::string cause) {
   OpThr_Close(dev, cause);
   OpThr_DisposeNoClose(dev, std::move(cause));
}
void Device::OpThr_DisposeNoClose(Device& dev, std::string cause) {
   if (dev.OpImpl_SetState(State::Disposing, &cause))
      dev.OpImpl_Dispose(std::move(cause));
}

void Device::OpImpl_StartRecv(RecvBufferSize preallocSize) {
   (void)preallocSize;
}
void Device::OpThr_SetLinkReady(Device& dev, std::string stmsg) {
   if (!dev.OpImpl_SetState(State::LinkReady, &stmsg))
      return;
   // 避免在處理 OnDevice_StateChanged() 之後 State_ 被改變(例:特殊情況下關閉了 Device),
   // 所以在此再判斷一次 State_, 必需仍為 LinkReady, 才需觸發 OnDevice_LinkReady() 事件.
   if (dev.State_ == State::LinkReady) {
      dev.CommonTimer_.StopAndWait();
      RecvBufferSize preallocSize = dev.Session_->OnDevice_LinkReady(dev);
      if (preallocSize == RecvBufferSize::NoRecvEvent)
         dev.Options_.Flags_ |= DeviceFlag::NoRecvEvent;
      else
         dev.Options_.Flags_ -= DeviceFlag::NoRecvEvent;
      dev.OpImpl_StartRecv(preallocSize);
   }
}
void Device::OpThr_SetBrokenState(Device& dev, std::string cause) {
   switch (dev.State_) {
   case State::Initializing:
   case State::Initialized:
   case State::ConfigError:
      //if (this->State_ < State::Opening   //尚未開啟
   case State::Closing:
   case State::Closed:
   case State::Disposing:
   case State::Destructing:
      // || State::Closing <= this->State_) //已經關閉(or 關閉中)
      // 不理會 Broken 狀態.
   case State::LinkError:
   case State::LinkBroken:
   case State::ListenBroken:
      // 已經是 Broken or Error, 不處理 Broken 狀態.
      return;

   case State::Lingering:
      OpThr_Close(dev, std::move(cause));
      break;
   case State::Opening:
   case State::WaitingLinkIn:
   case State::Linking:
      //if (State::Opening <= this->State_ && this->State_ <= State::Linking)
      dev.OpImpl_SetState(State::LinkError, &cause);
      break;
   case State::LinkReady:
      dev.OpImpl_SetState(State::LinkBroken, &cause);
      break;
   case State::Listening:
      dev.OpImpl_SetState(State::ListenBroken, &cause);
      break;
   }
}

void Device::OpImpl_StateChanged(const StateChangedArgs& e) {
   (void)e;
}
bool Device::OpImpl_SetState(State afst, StrView stmsg) {
   struct Aux {
      static bool CheckStartTimer(Device& dev, State afst) {
         uint32_t msTimerInterval;
         if (afst == State::LinkBroken || afst == State::ListenBroken)
            msTimerInterval = dev.Options_.LinkBrokenReopenInterval_;
         else if (afst == State::LinkError)
            msTimerInterval = dev.Options_.LinkErrorRetryInterval_;
         else if (afst == State::Closed)
            msTimerInterval = dev.Options_.ClosedReopenInterval_;
         else {
            return false;
         }
         if (msTimerInterval <= 0u)
            return false;
         dev.CommonTimer_.RunAfter(TimeInterval_Millisecond(msTimerInterval));
         return true;
      }
   };
   State bfst = this->State_;
   if (bfst == afst) {
      if (afst < State::Disposing) { // Disposing 訊息不需要重複更新.
         Aux::CheckStartTimer(*this, afst);
         StateUpdatedArgs e{afst, stmsg, *this->DeviceId_.Lock()};
         this->Session_->OnDevice_StateUpdated(*this, e);
         if (this->Manager_)
            this->Manager_->OnDevice_StateUpdated(*this, e);
      }
      return false;
   }
   if (bfst >= State::Disposing && afst < bfst)
      // 狀態 >= State::Disposing 之後, 就不可再回頭!
      return false;
   this->State_ = afst;
   StateChangedArgs e{stmsg, *this->DeviceId_.Lock()};
   e.BeforeState_ = bfst;
   e.After_.State_ = afst;
   this->OpImpl_StateChanged(e);

   if (!Aux::CheckStartTimer(*this, afst))
      this->CommonTimer_.StopNoWait();

   this->Session_->OnDevice_StateChanged(*this, e);
   if (this->Manager_)
      this->Manager_->OnDevice_StateChanged(*this, e);
   return true;
}

// 到 Op thread 取得 DeviceId, 然後才返回.
// - 如果是在 OnDevice_* 事件(除了: OnDevice_Initialized(), OnDevice_Destructing()) 裡呼叫,
//   表示已在 Op thread, 則會立即取得&返回.
// - 因為「取得 DeviceId」不是常用操作, 所以不考慮速度.
// - 由於可能會進入等待, 所以呼叫前不應有任何 lock, 須謹慎考慮死結問題.
// =====> 許多協定會在收到資料後 lock 某些資源,
// =====> 然後取得 DeviceId, 所以很容易陷入死結,
// =====> 因此將 WaitGetDeviceId() 改成 MustLock<> 方式處理;
//std::string Device::WaitGetDeviceId() {
//   std::string res;
//   this->OpQueue_.InplaceOrWait(AQueueTaskKind::Get,
//                                DeviceAsyncOp{[&res](Device& dev) {
//      res = dev.DeviceId_;
//   }});
//   return res;
//}

void Device::OpImpl_AppendDeviceInfo(std::string& info) {
   (void)info;
}
std::string Device::WaitGetDeviceInfo() {
   std::string res;
   res.reserve(300);
   res.append("|tm=");
   this->OpQueue_.InplaceOrWait(AQueueTaskKind::Get,
                                DeviceAsyncOp{[&res](Device& dev) {
      if (const char* tmstr = ToStrRev_Full(UtcNow()))
         res.append(tmstr, kDateTimeStrWidth);
      res.append("|st=");
      GetStateStr(dev.State_).AppendTo(res);
      res.append("|id={");
      res.append(*dev.DeviceId_.Lock());
      res.append("}|info=");
      dev.OpImpl_AppendDeviceInfo(res);
   }});
   return res;
}

std::string Device::WaitSetProperty(StrView strTagValueList) {
   struct Parser : public ConfigParser {
      fon9_NON_COPY_NON_MOVE(Parser);
      Device&        Device_;
      CountDownLatch Waiter_{1};
      RevBufferList  RBuf_{128};
      Parser(Device& dev) : Device_(dev) {}
      Result OnTagValue(StrView tag, StrView& value) override {
         return this->Device_.OpImpl_SetProperty(tag, value);
      }
      bool OnErrorBreak(ErrorEventArgs& e) override {
         RevPrint(this->RBuf_, "err=", e, '\n');
         return false;
      }
   };
   Parser pr{*this};
   this->OpQueue_.AddTask(DeviceAsyncOp{[&](Device&) {
      pr.Parse(strTagValueList);
      pr.Waiter_.ForceWakeUp();
   }});
   pr.Waiter_.Wait();
   return BufferTo<std::string>(pr.RBuf_.MoveOut());
}

ConfigParser::Result Device::OpImpl_SetProperty(StrView tag, StrView& value) {
   return this->Options_.OnTagValue(tag, value);
}

std::string Device::OnDevice_Command(StrView cmd, StrView param) {
   if (cmd == "open")
      this->AsyncOpen(param.ToString());
   else if (cmd == "close")
      this->AsyncClose(param.ToString("DeviceCommand.close:"));
   else if (cmd == "lclose")
      this->AsyncLingerClose(param.ToString("DeviceCommand.lclose:"));
   else if (cmd == "dispose")
      this->AsyncDispose(param.ToString("DeviceCommand.dispose:"));
   else if (cmd == "info")
      return this->WaitGetDeviceInfo();
   else if (cmd == "set")
      return this->WaitSetProperty(param);
   else if (cmd == "ses")
      return this->Session_->SessionCommand(*this, param);
   else if (cmd == "?") {
      static const char cstrHelp[] =
"open"    fon9_kCSTR_CELLSPL "Open device"         fon9_kCSTR_CELLSPL "[ConfigString] or Default config." fon9_kCSTR_ROWSPL
"close"   fon9_kCSTR_CELLSPL "Close device"        fon9_kCSTR_CELLSPL "close reason."                     fon9_kCSTR_ROWSPL
"lclose"  fon9_kCSTR_CELLSPL "Linger close device" fon9_kCSTR_CELLSPL "close reason."                     fon9_kCSTR_ROWSPL
"dispose" fon9_kCSTR_CELLSPL "Dispose device"      fon9_kCSTR_CELLSPL "dispose reason."                   fon9_kCSTR_ROWSPL
"info"    fon9_kCSTR_CELLSPL "Get device info"                                                            fon9_kCSTR_ROWSPL
"set"     fon9_kCSTR_CELLSPL "Set device property" fon9_kCSTR_CELLSPL "PropertyName=Value"                fon9_kCSTR_ROWSPL;
      std::string sesCommandHelp = this->Session_->SessionCommand(*this, "?");
      if (sesCommandHelp.find(fon9_kCSTR_CELLSPL) == std::string::npos) // 沒有 fon9_kCSTR_CELLSPL 表示不支援用 "?" 取得 help info.
         return std::string(cstrHelp, sizeof(cstrHelp) - 2); // -2: 移除尾端 "\n\0"
      std::string res{cstrHelp};
      res.append("-" fon9_kCSTR_ROWSPL);
      StrView sesCommands{&sesCommandHelp};
      while (!sesCommands.empty()) {
         StrView  sesCmd = StrFetchNoTrim(sesCommands, *fon9_kCSTR_ROWSPL);
         if (!sesCmd.empty()) {
            res.append("ses ");
            res.append(sesCmd.begin(), sesCmd.size() + 1); // +1 = fon9_kCSTR_ROWSPL;
         }
      }
      while (res.back() == '\0' || res.back() == *fon9_kCSTR_ROWSPL) {
         res.pop_back();
      }
      return res;
   }
   else return std::string{"unknown device command"};
   return std::string();
}
std::string Device::DeviceCommand(StrView cmdln) {
   StrView cmd = StrFetchTrim(cmdln, &isspace);
   return this->OnDevice_Command(cmd, StrTrim(&cmdln));
}

//--------------------------------------------------------------------------//

enum class StateTimerAct {
   Ignore,
   Reopen,
   ToDerived,
   ToSession,
};
static StateTimerAct GetStateTimerAct(const DeviceOptions& opts, State st) {
   switch (st) {
   case State::Initializing:
   case State::ConfigError:
   case State::Disposing:
   case State::Destructing:
      return StateTimerAct::Ignore;

   case State::LinkError:
      return (opts.LinkErrorRetryInterval_ > 0 ? StateTimerAct::Reopen : StateTimerAct::Ignore);
   case State::LinkBroken:
   case State::ListenBroken:
      return (opts.LinkBrokenReopenInterval_ > 0 ? StateTimerAct::Reopen : StateTimerAct::Ignore);
   case State::Closed:
      return (opts.ClosedReopenInterval_ > 0 ? StateTimerAct::Reopen : StateTimerAct::Ignore);

   case State::Initialized:
   case State::LinkReady:
      return StateTimerAct::ToSession;

   default:
   case State::Lingering:
   case State::Opening:
   case State::WaitingLinkIn:
   case State::Linking:
   case State::Listening:
   case State::Closing:
      return StateTimerAct::ToDerived;
   }
}
void Device::EmitOnCommonTimer(TimerEntry* timer, TimeStamp now) {
   Device& rthis = ContainerOf(*static_cast<CommonTimer*>(timer), &Device::CommonTimer_);
   switch (GetStateTimerAct(rthis.Options_, rthis.State_)) {
   default:
   case StateTimerAct::Ignore:
      break;
   case StateTimerAct::Reopen:
      rthis.OpQueue_.AddTask(DeviceAsyncOp{[](Device& dev) {
         if (GetStateTimerAct(dev.Options_, dev.State_) == StateTimerAct::Reopen) {
            // dev.OpImpl_Reopen(); 20220516: 使用 OpThr_Open(dev, std::string{}) 來執行 Reopen;
            // 讓 dev.Session_->OnDevice_BeforeOpen(); 在 reopen 時, 也有機會執行.
            OpThr_Open(dev, std::string{});
         }
      }});
      break;
   case StateTimerAct::ToDerived:
      rthis.OnCommonTimer(now);
      break;
   case StateTimerAct::ToSession:
      rthis.Session_->OnDevice_CommonTimer(rthis, now);
      break;
   }
}
void Device::OnCommonTimer(TimeStamp now) {
   (void)now;
}

} } // namespaces
