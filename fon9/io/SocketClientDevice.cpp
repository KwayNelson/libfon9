﻿/// \file fon9/io/SocketClientDevice.cpp
/// \author fonwinz@gmail.com
#include "fon9/io/SocketClientDevice.hpp"
#include "fon9/io/DeviceParseConfig.hpp"

namespace fon9 { namespace io {

SocketClientDevice::~SocketClientDevice() {
}
void SocketClientDevice::OpImpl_SocketClearLinking() {
   AsyncDnQuery_CancelAndWait(&this->DnReqId_);
   ZeroStruct(this->RemoteAddress_);
   this->NextAddrIndex_ = 0;
   this->AddrList_.clear();
   this->CommonTimer_.StopNoWait();
}
void SocketClientDevice::OnCommonTimer(TimeStamp now) {
   (void)now;
   this->OpQueue_.AddTask(DeviceAsyncOp{[](Device& dev) {
      SocketClientDevice*  pthis = static_cast<SocketClientDevice*>(&dev);
      if (pthis->DnReqId_) {
         AsyncDnQuery_CancelAndWait(&pthis->DnReqId_);
         OpThr_SetBrokenState(dev, "DN query timeout.");
      }
      else if (pthis->OpImpl_GetState() == State::Linking)
         pthis->OpImpl_ConnectToNext("Connect timeout.");
   }});
}
bool SocketClientDevice::SetSocketOptions(Socket& soCli, SocketResult& soRes) {
   return soCli.SetSocketOptions(this->Config_.Options_, this->Config_.GetAF(), soRes)
      && (this->Config_.AddrBind_.IsEmpty() || soCli.Bind(this->Config_.AddrBind_, soRes));
}
void SocketClientDevice::OpImpl_ConnectToNext(StrView lastError) {
   // 為了讓 LinkError or LinkBroken 能夠從 log 訊息辨認連線位置, 所以在此不清除 DeviceId.
   // OpThr_SetDeviceId(*this, std::string{});
   if (this->NextAddrIndex_ >= this->AddrList_.size()) {
      // 全部的 addr 都嘗試過, 才進入 LinkError 狀態.
      this->NextAddrIndex_ = 0;
      if (lastError.empty())
         lastError = (this->AddrList_.empty() ? StrView{"No hosts."} : StrView{"All hosts cannot connect."});
      this->OpImpl_SetState(this->OpImpl_GetState() == State::LinkReady
                            ? State::LinkBroken : State::LinkError, lastError);
      return;
   }

   const SocketAddress* addrRemote = &this->AddrList_[this->NextAddrIndex_++];
   const SocketAddress* addrLocal = this->Config_.AddrBind_.IsEmpty() ? nullptr : &this->Config_.AddrBind_;
   char                 strbuf[kMaxTcpConnectionUID];
   OpThr_SetDeviceId(*this, MakeTcpConnectionUID(strbuf, addrRemote, addrLocal).ToString());

   std::string  strmsg = "Connecting to: " + this->OpImpl_GetDeviceId();
   Socket       soCli;
   SocketResult soRes;
   if (this->CreateSocket(soCli, *addrRemote, soRes)
    && this->SetSocketOptions(soCli, soRes) ) {
      if (!lastError.empty()) {
         if (this->NextAddrIndex_ == 1)
            strmsg.append("|prev.err=");
         else {
            strmsg.append("|prev=");
            strmsg.append(strbuf, this->RemoteAddress_.ToAddrPortStr(strbuf));
            strmsg.append("|err=");
         }
         lastError.AppendTo(strmsg);
      }
      this->OpImpl_SetState(State::Linking, &strmsg);
      this->StartConnectTimer();
      strmsg.clear();
      this->RemoteAddress_ = *addrRemote;
      if (this->OpImpl_SocketConnect(std::move(soCli), soRes))
         return;
   }
   else
      strmsg.push_back('|');
   RevPrintAppendTo(strmsg, soRes);
   this->OpImpl_SetState(State::LinkError, &strmsg);
}
void SocketClientDevice::OpImpl_Open(std::string cfgstr) {
   this->OpImpl_SocketClearLinking();
   if (OpThr_DeviceParseConfig(*this, &cfgstr))
      this->OpImpl_ReopenImpl();
}
void SocketClientDevice::OpImpl_Reopen() {
   this->OpImpl_SocketClearLinking();
   this->OpImpl_ReopenImpl();
}
void SocketClientDevice::OpImpl_OnDnQueryDone(DnQueryReqId id, const DomainNameParseResult& res) {
   if (this->DnReqId_ != id)
      return;
   this->DnReqId_ = 0;
   this->AddrList_.insert(this->AddrList_.end(), res.AddressList_.begin(), res.AddressList_.end());
   this->OpImpl_ConnectToNext(&res.ErrMsg_);
}
void SocketClientDevice::OpImpl_ReopenImpl() {
   if (!this->Config_.AddrRemote_.IsAddrAny())
      this->AddrList_.emplace_back(this->Config_.AddrRemote_);
   if (!this->Config_.DomainNames_.empty()) {
      this->OpImpl_SetState(State::Linking, ToStrView("DN querying: " + this->Config_.DomainNames_));
      this->StartConnectTimer();
      DeviceSP pthis{this};
      AsyncDnQuery(this->DnReqId_, this->Config_.DomainNames_, this->Config_.AddrRemote_.GetPort(),
                   [pthis](DnQueryReqId id, DomainNameParseResult& res) {
         pthis->OpQueue_.AddTask(DeviceAsyncOp{[id, res](Device& dev) {
            static_cast<SocketClientDevice*>(&dev)->OpImpl_OnDnQueryDone(id, res);
         }});
      });
      return;
   }
   if (this->AddrList_.empty())
      this->OpImpl_OnAddrListEmpty();
   else
      this->OpImpl_ConnectToNext(StrView{});
}
void SocketClientDevice::OpImpl_OnAddrListEmpty() {
   this->OpImpl_SetState(State::ConfigError, "Config error: no remote address.");
}
void SocketClientDevice::OpImpl_SetConnected(std::string deviceId) {
   OpThr_SetDeviceId(*this, deviceId);
   OpThr_SetLinkReady(*this, std::string{});
}
void SocketClientDevice::OpImpl_Close(std::string cause) {
   this->OpImpl_SocketClearLinking();
   this->OpImpl_SetState(State::Closed, &cause);
   OpThr_SetDeviceId(*this, std::string{});
}
void SocketClientDevice::OpImpl_StateChanged(const StateChangedArgs& e) {
   if (IsAllowContinueSend(e.BeforeState_)) {
      if (e.After_.State_ != State::Lingering)
         this->OpImpl_SocketLinkBroken();
   }
   else if (e.BeforeState_ == State::Linking && e.After_.State_ != State::LinkReady)
      this->OpImpl_SocketClearLinking();
   base::OpImpl_StateChanged(e);
}

} } // namespaces
