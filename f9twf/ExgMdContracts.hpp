﻿// \file f9twf/ExgMdContracts.hpp
// \author fonwinz@gmail.com
#ifndef __f9twf_ExgMdContracts_hpp__
#define __f9twf_ExgMdContracts_hpp__
#include "f9twf/ExgTypes.hpp"
#include "fon9/ConfigUtils.hpp"
#include "fon9/SortedVector.hpp"

fon9_BEFORE_INCLUDE_STD
#include <vector>
#include <memory>
fon9_AFTER_INCLUDE_STD

namespace f9twf {

class f9twf_API ExgMdSymb;
class f9twf_API ExgMdSymbs;
using ContractSize = fon9::Decimal<uint64_t, 4>;
//--------------------------------------------------------------------------//
class f9twf_API ExgMdContract {
   fon9_NON_COPY_NON_MOVE(ExgMdContract);

   bool AssignBaseFromSymb(const ExgMdSymb& symb);

public:
   const ContractId  ContractId_;
   char              Padding___[5];
   /// 現貨標的.
   StkNo             StkNo_;
   /// 是否可報價.
   fon9::EnabledYN   AcceptQuote_{};

   uint32_t          PriceOrigDiv_{0};
   uint32_t          StrikePriceDiv_{0};
   /// 契約乘數.
   ContractSize      ContractSize_{};

   using Symbs = std::vector<ExgMdSymb*>;
   Symbs Symbs_;

   ExgMdContract(const ContractId conId)
      : ContractId_{conId} {
   }

   /// 在 Symbs_->LoadFrom() 之後, 設定 Contracts;
   void OnSymbsReload();
   /// 通常用在移除過期商品.
   void OnSymbRemove(ExgMdSymb& symb);
   /// 當商品基本資料有異動時, 通知 Contract.
   /// - 如果 Contract 尚未收到基本資料, 則可從 symb 取得部分基本資料.
   ///   例如: StrikePriceDiv_; PriceOrigDiv_;
   void OnSymbBaseChanged(const ExgMdSymb& symb);
   void SetBaseValues(uint32_t priceOrigDiv, uint32_t strikePriceDiv);
   /// 在建立新的商品時, 將 Contract 的基本資料填入 symb;
   void AssignBaseToSymb(ExgMdSymb& symb) const;
};
//--------------------------------------------------------------------------//
class f9twf_API ExgMdContracts {
   fon9_NON_COPY_NON_MOVE(ExgMdContracts);

   using ContractSP = std::unique_ptr<ExgMdContract>;
   struct Comper {
      bool operator()(const ContractSP& lhs, const ContractSP& rhs) const {
         return lhs->ContractId_ < rhs->ContractId_;
      }
      bool operator()(const ContractSP& lhs, ContractId rhs) const {
         return lhs->ContractId_ < rhs;
      }
      bool operator()(ContractId lhs, const ContractSP& rhs) const {
         return lhs < rhs->ContractId_;
      }
   };
   using ContractMap = fon9::SortedVectorSet<ContractSP, Comper>;
   ContractMap ContractMap_;

public:
   ExgMdContracts();

   /// 在系統重啟時, Symbs 重新載入後, 重新設定 Contracts 的基本資料.
   void OnSymbsReload();

   /// 必須在 Symbs locked 狀態下呼叫.
   /// 建立(or取得) Contract.
   /// - 在建立 symb 時呼叫.
   /// - 並將 symb 加入 Contract: retval->Symbs_.push_back(&symb);
   ExgMdContract& FetchContract(ExgMdSymb& symb);
   ExgMdContract& FetchContract(ContractId conId);
   ExgMdContract& FetchContract(fon9::StrView symbid) {
      if (fon9_LIKELY(symbid.size() > 3))
         symbid.SetEnd(symbid.begin() + 3);
      return this->FetchContract(ContractId{symbid});
   }
};

} // namespaces
#endif//__f9twf_ExgMdContracts_hpp__
