﻿// \file fon9/PkCont.hpp
// \author fonwinz@gmail.com
#ifndef __fon9_PkCont_hpp__
#define __fon9_PkCont_hpp__
#include "fon9/SortedVector.hpp"
#include "fon9/Timer.hpp"

namespace fon9 {

/// \ingroup Misc.
/// 確保收到封包的連續性.
/// - 可能有多個資訊源, 但序號相同.
/// - 即使同一個資訊源封包也可能亂序.
/// - 若封包序號小於期望, 視為重複封包, 拋棄之.
/// - 若封包序號不連續, 則等候一小段時間, 若仍沒收到連續封包, 則放棄等候.
/// - 衍生者解構時應主動呼叫 Clear(); 因為可能正在處理 PkContOnTimer();
class fon9_API PkContFeeder {
   fon9_NON_COPY_NON_MOVE(PkContFeeder);
public:
   using SeqT = uint64_t;

   PkContFeeder();
   virtual ~PkContFeeder();

   void Clear();

   /// 收到的封包透過這裡處理.
   /// - 若封包有連續, 則透過 PkContOnReceived() 通知衍生者.
   /// - 若封包不連續:
   ///   - 若 this->NextSeq_ == 0, 則直接轉 this->PkContOnReceived(); 由衍生者處理.
   ///   - 等候一小段時間(this->WaitInterval_), 若無法取得連續封包, 則強制繼續處理.
   void FeedPacket(const void* pk, unsigned pksz, SeqT seq);

   /// 如果封包序號不連續, 要等候多少時間? 才觸發「繼續處理」事件.
   void SetWaitInterval(TimeInterval v) {
      this->WaitInterval_ = v;
   }

   using FnOnLogSeqGap = void (*)(PkContFeeder& rthis, RevBuffer& rbuf, const void* pk);
   /// 若有需要記錄封包跳號, 則需要衍生者主動:
   /// 在 void PkContOnReceived(const void* pk, unsigned pksz, SeqT seq) override;
   /// 呼叫 this->CheckLogLost();
   void CheckLogLost(const void* pk, SeqT seq, FnOnLogSeqGap fnOnLogSeqGap) {
      if (auto lostCount = (seq - this->NextSeq_))
         this->LogSeqGap(pk, seq, lostCount, fnOnLogSeqGap);
   }
   SeqT ReceivedCount() const { return this->ReceivedCount_; }
   SeqT DroppedCount() const { return this->DroppedCount_; }
   SeqT LostCount() const { return this->LostCount_; }

protected:
   SeqT           NextSeq_{0};
   /// 收到的封包數量 = 呼叫 this->PkContOnReceived() 的次數.
   SeqT           ReceivedCount_{0};
   /// 重複(收到的 Seq < 期望的序號)的封包數量.
   /// 這類封包會直接拋棄.
   SeqT           DroppedCount_{0};
   /// 遺失的封包數量: 根據 Seq 計算.
   /// 在呼叫 this->LogSeqGap() 之前, 不含第一個封包前的遺失數量.
   SeqT           LostCount_{0};
   /// 預計在 PkContOnReceived(..., seq) 處理完後的下一個封包序號.
   /// 您可以在 PkContOnReceived() 返回前改變此值.
   /// 預設為 seq + 1;
   SeqT           AfterNextSeq_;
   TimeInterval   WaitInterval_{TimeInterval_Millisecond(5)};
   
   struct PkRec : public std::string {
      SeqT  Seq_;
      PkRec(SeqT seq) : Seq_{seq} {
      }
      bool operator<(const PkRec& rhs) const {
         return this->Seq_ < rhs.Seq_;
      }
   };
   using PkPendingsImpl = SortedVectorSet<PkRec>;
   using PkPendings = MustLock<PkPendingsImpl>;
   PkPendings  PkPendings_;

   virtual void PkContOnTimer(PkPendings::Locker&& pks);
   static void EmitOnTimer(TimerEntry* timer, TimeStamp now);
   DataMemberEmitOnTimer<&PkContFeeder::EmitOnTimer> Timer_;
   void StartPkContTimer() {
      this->Timer_.RunAfter(this->WaitInterval_);
   }

   /// 序號小於期望, 在 ++this->DroppedCount_; 之後, 呼叫此處, 預設 do nothing;
   virtual void PkContOnDropped(const void* pk, unsigned pksz, SeqT seq);

   /// 通知衍生者收到的封包:
   /// - 當收到連續封包時.
   /// - 當超過指定時間沒有連續時.
   /// - 此時的 this->NextSeq_ 尚未改變, 仍維持前一次的預期序號.
   ///   因此可以判斷 this->NextSeq_ == seq 表示序號連續.
   /// - 為了確保資料連續性, 呼叫此處時會將 this->PkPendings_ 鎖住.
   ///   所以不會在不同 thread 重複進入 PkContOnReceived();
   virtual void PkContOnReceived(const void* pk, unsigned pksz, SeqT seq) = 0;

   void CallOnReceived(const void* pk, unsigned pksz, SeqT seq) {
      this->AfterNextSeq_ = seq + 1;
      this->PkContOnReceived(pk, pksz, seq);
      this->NextSeq_ = this->AfterNextSeq_;
      ++this->ReceivedCount_;
   }

   void ResetBiggerNextSeq(SeqT newNextSeq) {
      if (this->NextSeq_ < newNextSeq) {
         this->LostCount_ += (newNextSeq - this->NextSeq_);
         this->NextSeq_ = newNextSeq;
      }
   }

   void LogSeqGap(const void* pk, SeqT seq, SeqT lostCount, FnOnLogSeqGap fnOnLogSeqGap);
};
//--------------------------------------------------------------------------//
template <class PkSysT, class PkTypeT>
class PkHandlerBase {
   fon9_NON_COPY_NON_MOVE(PkHandlerBase);
public:
   using PkType = PkTypeT;
   PkSysT&  PkSys_;

   PkHandlerBase(PkSysT& pkSys) : PkSys_(pkSys) {
   }
   virtual ~PkHandlerBase() {
   }
   virtual void OnPkReceived(const PkType& pk, unsigned pksz) = 0;
   virtual void DailyClear() = 0;
};

template <class PkHandlerBase>
class PkHandlerAnySeq : public PkHandlerBase {
   fon9_NON_COPY_NON_MOVE(PkHandlerAnySeq);
public:
   using PkHandlerBase::PkHandlerBase;
   /// do nothing;
   void DailyClear() override {
   }
};

template <class PkHandlerBase>
class PkHandlerPkCont : public PkHandlerBase, public PkContFeeder {
   fon9_NON_COPY_NON_MOVE(PkHandlerPkCont);
public:
   using PkType = typename PkHandlerBase::PkType;
   using PkHandlerBase::PkHandlerBase;
   virtual ~PkHandlerPkCont() {
   }

   // 轉呼叫: PkContFeeder::FeedPacket(&pk, pksz, pk.GetSeqNo());
   void OnPkReceived(const PkType& pk, unsigned pksz) override {
      this->FeedPacket(&pk, pksz, pk.GetSeqNo());
   }
   // 轉呼叫: PkContFeeder::Clear();
   void DailyClear() override {
      this->Clear();
   }
};

} // namespaces
#endif//__fon9_PkCont_hpp__
