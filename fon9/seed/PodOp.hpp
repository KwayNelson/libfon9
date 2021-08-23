﻿/// \file fon9/seed/PodOp.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_seed_PodOp_hpp__
#define __fon9_seed_PodOp_hpp__
#include "fon9/seed/Tree.hpp"

namespace fon9 { namespace seed {

class fon9_API SeedVisitor;

class fon9_API PodOp : public SubscribableOp {
   fon9_NON_COPY_NON_MOVE(PodOp);
protected:
   virtual ~PodOp();

public:
   PodOp() = default;

   /// 取得現有的 Sapling, 若不存在或尚未建立, 則傳回 nullptr.
   virtual TreeSP GetSapling(Tab& tab);

   /// 若不存在則嘗試建立, 否則傳回現有的 Sapling.
   /// 預設: 直接呼叫 this->GetSapling(tab);
   virtual TreeSP MakeSapling(Tab& tab);

   /// 對此 Pod(or Seed) 進行指令操作.
   /// 不一定會在返回前呼叫 resHandler, 也不一定會在同一個 thread 呼叫 resHandler, 由衍生者決定.
   virtual void OnSeedCommand(Tab* tab, StrView cmdln, FnCommandResultHandler resHandler) = 0;
   /// 通過 TicketRunnerCommand 來執行的, 則會提供 Visitor, 可用來取得相關資訊, 例: visitor->GetUFrom();
   /// - 用以因應某些指令必須檢查使用者的權限.
   /// - 預設直接轉給 this->OnSeedCommand(); 執行.
   virtual void OnVisitorCommand(Tab* tab, StrView cmdln, FnCommandResultHandler resHandler, SeedVisitor& visitor);

   virtual void BeginRead(Tab& tab, FnReadOp fnCallback) = 0;
   virtual void BeginWrite(Tab& tab, FnWriteOp fnCallback) = 0;
};

//--------------------------------------------------------------------------//

class fon9_API PodOpDefault : public PodOp, public SeedOpResult {
   fon9_NON_COPY_NON_MOVE(PodOpDefault);
public:
   using SeedOpResult::SeedOpResult;

   /// 對此 Seed 進行指令操作.
   /// 不一定會在返回前呼叫 resHandler, 也不一定會在同一個 thread 呼叫 resHandler, 由衍生者決定.
   /// 預設: 直接呼叫 resHandler(OpResult::not_supported_cmd, cmdpr);
   void OnSeedCommand(Tab* tab, StrView cmdln, FnCommandResultHandler resHandler) override;

   void BeginRead(Tab& tab, FnReadOp fnCallback) override;
   void BeginWrite(Tab& tab, FnWriteOp fnCallback) override;

   template <class FnOp, class RawRW>
   void BeginRW(Tab& tab, FnOp&& fnCallback, RawRW&& op) {
      assert(this->Sender_->LayoutSP_->GetTab(static_cast<size_t>(tab.GetIndex())) == &tab);
      this->Tab_ = &tab;
      this->OpResult_ = OpResult::no_error;
      fnCallback(*this, &op);
   }

   void OnSeedCommand_NotSupported(Tab* tab, StrView cmdln, FnCommandResultHandler&& resHandler);
   void BeginRead_NotSupported(Tab& tab, FnReadOp&& fnCallback);
   void BeginWrite_NotSupported(Tab& tab, FnWriteOp&& fnCallback);
};

template <class Pod>
class PodOpReadonly : public PodOpDefault {
   fon9_NON_COPY_NON_MOVE(PodOpReadonly);
   using base = PodOpDefault;
public:
   Pod&  Pod_;
   PodOpReadonly(Pod& pod, Tree& sender, const StrView& key)
      : base{sender, OpResult::no_error, key}
      , Pod_(pod) {
   }
   void BeginRead(Tab& tab, FnReadOp fnCallback) override {
      this->BeginRW(tab, std::move(fnCallback), SimpleRawRd{this->Pod_});
   }
};

} } // namespaces
#endif//__fon9_seed_PodOp_hpp__
