﻿// \file fon9/fmkt/MdSymbs.hpp
// \author fonwinz@gmail.com
#ifndef __fon9_fmkt_MdSymbs_hpp__
#define __fon9_fmkt_MdSymbs_hpp__
#include "fon9/fmkt/SymbTree.hpp"
#include "fon9/fmkt/MdRtStream.hpp"
#include "fon9/fmkt/FmdTypes.hpp"
#include "fon9/fmkt/SymbTabNames.h"

namespace fon9 { namespace fmkt {

void fon9_API SymbCellsToBitv(RevBuffer& rbuf, seed::Layout& layout, Symb& symb);
//--------------------------------------------------------------------------//
class fon9_API MdSymbsBase : public SymbTree {
   fon9_NON_COPY_NON_MOVE(MdSymbsBase);
   using base = SymbTree;
   struct Recover;

   struct SymbsSubr : public MdRtSubr {
      fon9_NON_COPY_NON_MOVE(SymbsSubr);
      using MdRtSubr::MdRtSubr;
      SymbMapImpl SymbsRecovering_;
   };
   struct SymbsSubrSP : public intrusive_ptr<SymbsSubr> {
      using base = intrusive_ptr<SymbsSubr>;
      using base::base;
      /// 呼叫前必須: lock tree, 檢查 IsUnsubscribed();
      void operator()(const seed::SeedNotifyArgs& e) const {
         assert(static_cast<SymbTree*>(&e.Tree_)->SymbMap_.IsLocked());
         assert(!this->get()->IsUnsubscribed());
         auto& symbs = this->get()->SymbsRecovering_;
         if (symbs.empty() || symbs.find(e.KeyText_) == symbs.end())
            this->get()->Callback_(e);
      }
   };
   using UnsafeSubj = seed::UnsafeSeedSubjT<SymbsSubrSP>;
   UnsafeSubj  UnsafeSubj_;

   /// 在 LoadFrom() 成功開啟, 且檔案載入完畢後的通知.
   /// 預設 do nothing.
   virtual void OnAfterLoadFrom(Locker&& symbsLk);

protected:
   bool  IsDailyClearing_{false};
   bool  IsBlockPublish_{false};
   char  Padding____[2];

public:
   const MdSymbsCtrlFlag   CtrlFlags_;
   seed::Tab* const        RtTab_;
   MdRtStreamInnMgr        RtInnMgr_;

   fon9_MSC_WARN_DISABLE(4355); // 'this': used in base member initializer list
   MdSymbsBase(seed::LayoutSP layout, std::string rtiPathFmt, MdSymbsCtrlFlag flags = MdSymbsCtrlFlag{})
      : base{std::move(layout)}
      , CtrlFlags_{flags}
      , RtTab_{LayoutSP_->GetTab(fon9_kCSTR_TabName_Rt)}
      , RtInnMgr_(*this, std::move(rtiPathFmt)) {
   }
   fon9_MSC_WARN_POP;

   ~MdSymbsBase();

   void SetDailyClearHHMMSS(unsigned hhmmss) {
      this->RtInnMgr_.SetDailyClearHHMMSS(hhmmss);
   }

   /// 訂閱整棵樹, 建構時必須提供 MdSymbsCtrlFlag::AllowSubrTree 旗標.
   seed::OpResult SubscribeStream(SubConn* pSubConn, seed::Tab&, StrView args, seed::FnSeedSubr&&);
   seed::OpResult UnsubscribeStream(SubConn, seed::Tab&);
   /// - 此處會檢查 assert(this->SymbMap_.IsLocked()); 未鎖定的呼叫, 必定為設計上的問題.
   /// - 若 pkType == f9sv_RtsPackType_Count; 則必須從 e.NotifyKind_ 取得通知種類.
   void UnsafePublish(f9sv_RtsPackType pkType, seed::SeedNotifyArgs& e);

   void DailyClear(unsigned tdayYYYYMMDD);

   /// 儲存現在的全部商品資料, 通常在程式結束前呼叫.
   void SaveTo(std::string fname);
   /// 載入商品資料, 通常在程式啟動時呼叫.
   void LoadFrom(std::string fname);

   /// 在某些情況下, 可以先暫停「整棵樹」的發行.
   /// 例如: 行情資料來源一個封包, 包含多筆商品資料,
   ///       此時可以先暫停, 避免每個商品都發行一次,
   ///       統一在整個封包都處理完畢後發行一次即可.
   struct BlockPublish {
      fon9_NON_COPY_NON_MOVE(BlockPublish);
      MdSymbsBase&   Owner_;
      RevBufferList  AckBuf_;
      BlockPublish(MdSymbsBase& owner, BufferNodeSize newAllocReserved)
         : Owner_(owner)
         , AckBuf_{newAllocReserved} {
         assert(owner.SymbMap_.IsLocked() && owner.IsBlockPublish_ == false);
         owner.IsBlockPublish_ = true;
      }
      ~BlockPublish() {
         assert(this->Owner_.SymbMap_.IsLocked());
         this->Owner_.IsBlockPublish_ = false;
      }
      RevBufferList* GetPublishBuffer() {
         return this->Owner_.UnsafeSubj_.IsEmpty() ? nullptr : &this->AckBuf_;
      }
      void UnsafePublish(f9sv_RtsPackType pkType, seed::SeedNotifyArgs& e) {
         assert(this->Owner_.SymbMap_.IsLocked());
         this->Owner_.IsBlockPublish_ = false;
         this->Owner_.UnsafePublish(pkType, e);
      }
   };
};
//--------------------------------------------------------------------------//
/// MdSymbs 提供:
/// - 協助訂閱即時資料.
/// - 協助使用 RtInnMgr_ 提供儲存即時資料.
/// - 協助 DailyClear;
/// - MdSymbT 必須提供:
///   - fon9::fmkt::MdRtStream  MdRtStream_;
template <class MdSymbT>
class MdSymbsT : public MdSymbsBase {
   fon9_NON_COPY_NON_MOVE(MdSymbsT);
   using base = MdSymbsBase;

public:
   using base::base;

   //-----------------------------//
   struct MdSymOp : public PodOp {
      fon9_NON_COPY_NON_MOVE(MdSymOp);
      using base = PodOp;
      using base::base;
      seed::OpResult SubscribeStream(SubConn* pSubConn, seed::Tab& tab, StrView args, seed::FnSeedSubr subr) override {
         if (static_cast<MdSymbsT*>(this->Sender_)->RtTab_ != &tab)
            return seed::SubscribeStreamUnsupported(pSubConn);
         return static_cast<MdSymbT*>(this->Symb_.get())->MdRtStream_.SubscribeStream(
            this->LockedMap_, pSubConn, tab, *this, args, std::move(subr));
      }
      seed::OpResult UnsubscribeStream(SubConn subConn, seed::Tab& tab) override {
         if (static_cast<MdSymbsT*>(this->Sender_)->RtTab_ != &tab)
            return seed::OpResult::not_supported_subscribe_stream;
         return static_cast<MdSymbT*>(this->Symb_.get())->MdRtStream_.UnsubscribeStream(
            this->LockedMap_, subConn);
      }
   };
   //-----------------------------//
   struct MdSymbsOp : public TreeOp {
      fon9_NON_COPY_NON_MOVE(MdSymbsOp);
      using TreeOp::TreeOp;
      void OnSymbPodOp(const StrView& strKeyText, SymbSP&& symb, seed::FnPodOp&& fnCallback, const Locker& lockedMap) override {
         MdSymOp op(this->Tree_, strKeyText, std::move(symb), lockedMap);
         fnCallback(op, &op);
      }
      seed::OpResult SubscribeStream(SubConn* pSubConn, seed::Tab& tab, StrView args, seed::FnSeedSubr subr) override {
         return static_cast<MdSymbsT*>(&this->Tree_)->SubscribeStream(pSubConn, tab, args, std::move(subr));
      }
      seed::OpResult UnsubscribeStream(SubConn subConn, seed::Tab& tab) override {
         return static_cast<MdSymbsT*>(&this->Tree_)->UnsubscribeStream(subConn, tab);
      }
   };
   //-----------------------------//
   void OnTreeOp(fon9::seed::FnTreeOp fnCallback) override {
      MdSymbsOp op{*this};
      fnCallback(seed::TreeOpResult{this, fon9::seed::OpResult::no_error}, &op);
   }
};

} } // namespaces
#endif//__fon9_fmkt_MdSymbs_hpp__
