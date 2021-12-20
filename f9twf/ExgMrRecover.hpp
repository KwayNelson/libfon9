﻿// \file f9twf/ExgMrRecover.hpp
// \author fonwinz@gmail.com
#ifndef __f9twf_ExgMrRecover_hpp__
#define __f9twf_ExgMrRecover_hpp__
#include "f9twf/ExgMrFmt.hpp"
#include "f9twf/ExgMdPkReceiver.hpp"
#include "fon9/framework/IoFactory.hpp"

namespace f9twf {

class f9twf_API ExgMcChannelMgr;
using ExgMcChannelMgrSP = fon9::intrusive_ptr<ExgMcChannelMgr>;

enum class ExgMcRecoverRole : char {
   Primary = 'P',
   Secondary = 'S',
};
enum ExgMrState : uint8_t {
   Linking,
   LinkBroken,
   ApReady,
   Requesting,
};

/// 台灣期交所逐筆行情 回補 Session.
/// 收到回補封包後, 直接轉給 ExgMcChannel;
class ExgMrRecoverSession : public fon9::io::Session, private ExgMcPkReceiver {
   fon9_NON_COPY_NON_MOVE(ExgMrRecoverSession);
   using base = fon9::io::Session;
   fon9::io::Device* Device_{};
   fon9::CharVector  ApReadyMsg_;
   ExgMrMsgSeqNum_t  MsgSeqNum_{};
   uint32_t          RequestCount_{0};
   ExgMrRecoverNum_t Recovering_{0};
   ExgMrState        State_{ExgMrState::Linking};

   /// 收到的回補封包, 直接轉給 ChannelMgr_;
   bool OnPkReceived(const void* pk, unsigned pksz) override;

   void OnDevice_Initialized(fon9::io::Device& dev) override;
   fon9::io::RecvBufferSize OnDevice_LinkReady(fon9::io::Device& dev) override;
   void OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) override;
   fon9::io::RecvBufferSize OnDevice_Recv(fon9::io::Device& dev, fon9::DcQueue& rxbuf) override;
   void OnDevice_CommonTimer(fon9::io::Device& dev, fon9::TimeStamp now) override;
   void SetDelRecover(fon9::io::Device& dev, fon9::StrView apStMsg);

public:
   const ExgMcRecoverRole     Role_;
   const ExgMrSessionId_t     SessionId_;
   const uint16_t             Pass_; // 0001..9999
   const ExgMcChannelMgrSP    ChannelMgr_;
   const fon9::TimeInterval   OverTimesDelay_;

   ExgMrRecoverSession(ExgMcChannelMgrSP  channelMgr,
                       ExgMcRecoverRole   role,
                       ExgMrSessionId_t   sessionId,
                       uint16_t           pass,
                       fon9::TimeInterval overTimesDelay)
      : Role_{role}
      , SessionId_{sessionId}
      , Pass_{pass}
      , ChannelMgr_{std::move(channelMgr)}
      , OverTimesDelay_{overTimesDelay} {
   }
   ~ExgMrRecoverSession();

   uint32_t GetRequestCount() const {
      return this->RequestCount_;
   }
   fon9::io::Device* GetDevice() const {
      return this->Device_;
   }
   ExgMrState GetState() const {
      return this->State_;
   }
   /// \retval ExgMrState::Requesting 成功送出要求.
   /// \retval ExgMrState::ApReady 有尚未完成的[回補要求], 須等上次的 [回補要求] 完成後才能再次要求回補.
   /// \retval else 尚未連線成功.
   ExgMrState RequestMcRecover(ExgMrChannelId_t channelId, ExgMrMsgSeqNum_t from, ExgMrRecoverNum_t count);
};

//--------------------------------------------------------------------------//
class f9twf_API ExgMrRecoverFactory : public fon9::SessionFactory {
   fon9_NON_COPY_NON_MOVE(ExgMrRecoverFactory);
   using base = fon9::SessionFactory;
public:
   using base::base;

   fon9::io::SessionSP CreateSession(fon9::IoManager& ioMgr, const fon9::IoConfigItem& cfg, std::string& errReason) override;
   fon9::io::SessionServerSP CreateSessionServer(fon9::IoManager& ioMgr, const fon9::IoConfigItem& cfg, std::string& errReason) override;
};

} // namespaces
#endif//__f9twf_ExgMrRecover_hpp__
