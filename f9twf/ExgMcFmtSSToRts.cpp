﻿// \file f9twf/ExgMcFmtSSToRts.cpp
// \author fonwinz@gmail.com
#include "f9twf/ExgMcFmtSSToRts.hpp"
#include "f9twf/ExgMcFmtSS.hpp"
#include "f9twf/ExgMdFmtParsers.hpp"
#include "fon9/BitvEncode.hpp"
#include "fon9/buffer/FwdBufferList.hpp"

namespace f9twf {
using namespace fon9;
using namespace fon9::fmkt;

static void McI084SSParserO(ExgMcMessage& e) {
   ExgMcChannel& rtch = e.Channel_.GetChannelMgr()->GetRealtimeChannel(e.Channel_);
   char          bufI083[sizeof(ExgMcI083) + sizeof(ExgMdEntry) * 100];
   ExgMcI083*    pI083 = nullptr;
   if (rtch.IsNeedsNotifyConsumer()) {
      // 轉發 ExgMcI083 訊息.
      // TODO: 也許應改成選項, 選擇: I084._O_ 是否要轉發 I083 訊息?
      // TODO: ToMiConv 可以直接訂閱 Channel 13,14 處理 I084._O_ 然後轉發 I080 封包.
      pI083 = reinterpret_cast<ExgMcI083*>(bufI083);
      *static_cast<ExgMcHead*>(pI083) = e.Pk_;
      pI083->MessageKind_ = 'B';
      fon9::ToPackBcd(pI083->ChannelId_, rtch.GetChannelId());
      // pI083->ChannelSeq_ 設為 0, 讓處理的人可以判斷此時的 I083 是從快照而來.
      fon9::ToPackBcd(pI083->ChannelSeq_, 0u);
      fon9::ToPackBcd(pI083->VersionNo_, 1u);
      // 試撮價格註記, '0':委託簿揭示訊息; '1':試撮後剩餘委託訊息.
      // 2020.01.17: 根據期交所說明, 試撮階段不會有委託簿快照訊息.
      pI083->CalculatedFlag_ = '0';
   }

   auto&    pk = static_cast<const ExgMcI084*>(&e.Pk_)->_O_OrderData_;
   auto     mdTime = e.Pk_.InformationTime_.ToDayTime();
   unsigned prodCount = fon9::PackBcdTo<unsigned>(pk.NoEntries_);
   auto*    prodEntry = pk.Entry_;
   auto*    symbs = e.Channel_.GetChannelMgr()->Symbs_.get();
   auto     locker = symbs->SymbMap_.Lock();
   for (unsigned prodL = 0; prodL < prodCount; ++prodL) {
      const auto  mdCount = fon9::PackBcdTo<unsigned>(prodEntry->NoMdEntries_);
      if (const auto  lastProdMsgSeq = PackBcdToMarketSeq(prodEntry->LastProdMsgSeq_)) {
         auto& symb = *static_cast<ExgMdSymb*>(symbs->FetchSymb(locker, fon9::StrView_eos_or_all(prodEntry->ProdId_.Chars_, ' ')).get());
         if (e.Channel_.GetChannelMgr()->CheckSymbTradingSessionId(symb)) {
            ExgMdToSnapshotBS(mdTime, mdCount, prodEntry->MdEntry_, symb, lastProdMsgSeq);
            RevBufferList rbuf{128};
            MdRtsPackSnapshotBS(rbuf, symb.BS_.Data_);
            PackMktSeq(rbuf, symbs->CtrlFlags_, symb.BS_.Data_.MarketSeq_);
            symb.MdRtStream_.Publish(ToStrView(symb.SymbId_), f9sv_RtsPackType_SnapshotBS, f9sv_MdRtsKind_BS, mdTime, std::move(rbuf));
            if (pI083) {
               static_assert(sizeof(pI083->NoMdEntries_) == 1, "");
               *pI083->NoMdEntries_ = *prodEntry->NoMdEntries_;
               memcpy(pI083->MdEntry_, prodEntry->MdEntry_, sizeof(ExgMdEntry) * mdCount);
               pI083->ProdId_ = prodEntry->ProdId_;
               memcpy(pI083->ProdMsgSeq_, prodEntry->LastProdMsgSeq_, sizeof(pI083->ProdMsgSeq_));
               const unsigned pksz = static_cast<unsigned>(sizeof(ExgMcI083) + sizeof(ExgMdTail)
                                                           + (sizeof(pI083->MdEntry_[0]) * mdCount)
                                                           - sizeof(pI083->MdEntry_));
               fon9::ToPackBcd(pI083->BodyLength_, pksz - sizeof(ExgMcNoBody));
               ExgMcMessage e083{*pI083, pksz, rtch, 0};
               e083.Symb_ = &symb;
               rtch.NotifyConsumers(e083);
            }
         }
      }
      prodEntry = reinterpret_cast<const ExgMcI084::OrderDataEntry*>(prodEntry->MdEntry_ + mdCount);
   }
}

static void McI084SSParserP(ExgMcMessage& e) {
   auto&    pk         = static_cast<const ExgMcI084*>(&e.Pk_)->_P_ProductStatus_;
   auto     mdTime     = e.Pk_.InformationTime_.ToDayTime();
   unsigned prodCount  = fon9::PackBcdTo<unsigned>(pk.NoEntries_);
   auto*    prodEntry  = pk.Entry_;
   auto*    symbs      = e.Channel_.GetChannelMgr()->Symbs_.get();

   struct LvLmtParser {
      fon9_NON_COPY_NON_MOVE(LvLmtParser);
      fon9::RevBufferList        Rts_{64};
      const fon9::seed::Tab&     TabRef_;
      const fon9::seed::Field&   FldLvUpLmt_;
      const fon9::seed::Field&   FldLvDnLmt_;
      LvLmtParser(fon9::seed::Layout& layout)
         : TabRef_(*layout.GetTab(fon9_kCSTR_TabName_Ref))
         , FldLvUpLmt_(*TabRef_.Fields_.Get("LvUpLmt"))
         , FldLvDnLmt_(*TabRef_.Fields_.Get("LvDnLmt")) {
      }
      bool SetLvLmt(int8_t& dst, const fon9::seed::Field& fld, uint8_t lv) {
         if (lv > 0)
            --lv;
         if (dst == lv)
            return false;
         fon9::fmkt::MdRtsPackFieldValueNid(this->Rts_, fld);
         return true;
      }
   };
   LvLmtParser parser{*symbs->LayoutSP_};
   auto        locker = symbs->SymbMap_.Lock();
   for (unsigned prodL = 0; prodL < prodCount; ++prodL) {
      auto& symb = *static_cast<ExgMdSymb*>(symbs->FetchSymb(locker, fon9::StrView_eos_or_all(prodEntry->ProdId_.Chars_, ' ')).get());
      bool  isChanged = parser.SetLvLmt(symb.Ref_.Data_.LvUpLmt_, parser.FldLvUpLmt_, fon9::PackBcdTo<uint8_t>(prodEntry->ExpandTypeLevelR_))
                      | parser.SetLvLmt(symb.Ref_.Data_.LvDnLmt_, parser.FldLvDnLmt_, fon9::PackBcdTo<uint8_t>(prodEntry->ExpandTypeLevelF_));
      if (isChanged) {
         symb.MdRtStream_.Publish(ToStrView(symb.SymbId_),
                                  f9sv_RtsPackType_FieldValue_AndInfoTime,
                                  f9sv_MdRtsKind_Ref,
                                  mdTime,
                                  std::move(parser.Rts_));
      }
   }
}

f9twf_API void I084SSParserToRts(ExgMcMessage& e) {
   switch (static_cast<const ExgMcI084*>(&e.Pk_)->MessageType_) {
   //case 'A': McI084SSParserA(e);  break;
     case 'O': McI084SSParserO(e);  break;
   //case 'S': McI084SSParserS(e);  break;
     case 'P': McI084SSParserP(e);  break;
   //case 'Z': McI084SSParserZ(e);  break;
   }
}

} // namespaces
