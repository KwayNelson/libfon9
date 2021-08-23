﻿// \file fon9/rc/RcSvc.cpp
// \author fonwinz@gmail.com
#include "fon9/rc/RcSvc.hpp"
#include "fon9/rc/RcSvcReq.hpp"
#include "fon9/rc/RcSvStreamDecoder.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/Log.hpp"

namespace fon9 { namespace rc { namespace svc {

TreeRec::~TreeRec() {
   delete[] this->LayoutC_.TabArray_;
}
void TreeRec::MakeSeed(SeedSP& seed, f9sv_TabSize tabidx) {
   assert(tabidx < this->LayoutC_.TabCount_);
   if (tabidx < this->LayoutC_.TabCount_)
      seed.reset(SeedRec::MakeSeedRec(*this->Layout_->GetTab(tabidx)));
   else
      seed.reset();
}
void TreeRec::MakePod(PodRec& pod) {
   pod.ClearSeeds();
   pod.Seeds_ = new SeedSP[this->LayoutC_.TabCount_];
   for (f9sv_TabSize tabidx = this->LayoutC_.TabCount_; tabidx > 0;) {
      --tabidx;
      this->MakeSeed(pod.Seeds_[tabidx], tabidx);
   }
}
PodRec& TreeRec::FetchPod(StrView seedKey, bool* isNew) {
   PodRec& pod = this->PodMap_[CharVector::MakeRef(seedKey)];
   if (pod.Seeds_ != nullptr) {
      if (isNew)
         *isNew = false;
   }
   else {
      if (isNew)
         *isNew = true;
      this->MakePod(pod);
   }
   return pod;
}
//--------------------------------------------------------------------------//
static void NamedToC(f9sv_Named& cNamed, const NamedIx& named) {
   cNamed.Name_ = ToStrView(named.Name_).ToCStrView();
   cNamed.Title_ = ToStrView(named.GetTitle()).ToCStrView();
   cNamed.Description_ = ToStrView(named.GetDescription()).ToCStrView();
   cNamed.Index_ = named.GetIndex();
}
static void FieldToC(f9sv_Field& cfld, const seed::Field& fld) {
   NamedToC(cfld.Named_, fld);
   NumOutBuf nbuf;
   fld.GetTypeId(nbuf).ToString(cfld.TypeId_);
   cfld.Size_ = fld.Size_;
   cfld.Offset_ = fld.Offset_;
   cfld.DecScale_ = fld.DecScale_;
   cfld.Type_ = static_cast<f9sv_FieldType>(cast_to_underlying(fld.Type_));
   cfld.NullValue_.Signed_ = fld.GetNullValue();
}
void TreeRec::ParseLayout(StrView cfgstr) {
   assert(this->Layout_.get() == nullptr);
   seed::TreeFlag treeFlags = static_cast<seed::TreeFlag>(HIntStrTo(StrFetchNoTrim(cfgstr, '|')));
   seed::FieldSP  fldKey{seed::MakeField(cfgstr, '|', '\n')};
   std::vector<seed::TabSP> tabs;
   while (!StrTrimHead(&cfgstr).empty()) {
      seed::TabFlag tabFlags = static_cast<seed::TabFlag>(HIntStrTo(StrFetchNoTrim(cfgstr, '|')));
      Named         tabNamed{DeserializeNamed(cfgstr, '|', '\n')};
      StrView       fldcfg = SbrTrimHeadFetchInside(cfgstr);
      seed::Fields  fields;
      seed::MakeFields(fldcfg, '|', '\n', fields);
      tabs.emplace_back(new seed::Tab{tabNamed, std::move(fields), tabFlags});
   }
   seed::LayoutSP layout{new seed::LayoutN{std::move(fldKey), treeFlags, std::move(tabs)}};
   FieldToC(this->LayoutC_.KeyField_, *layout->KeyField_);
   this->LayoutC_.TabCount_ = static_cast<f9sv_TabSize>(layout->GetTabCount());
   this->LayoutC_.TabArray_ = new f9sv_Tab[this->LayoutC_.TabCount_];
   this->TabArray_.resize(this->LayoutC_.TabCount_);
   f9sv_TabSize tabidx = 0;
   for (TabRec& rtab : this->TabArray_) {
      f9sv_Tab&  ctab = this->LayoutC_.TabArray_[tabidx];
      seed::Tab* stab = layout->GetTab(tabidx++);
      NamedToC(ctab.Named_, *stab);
      ctab.FieldCount_ = static_cast<unsigned>(stab->Fields_.size());
      rtab.Fields_.resize(ctab.FieldCount_);
      if (ctab.FieldCount_ == 0)
         ctab.FieldArray_ = nullptr;
      else {
         ctab.FieldArray_ = &*rtab.Fields_.begin();
         unsigned fldidx = 0;
         while (const auto* fld = stab->Fields_.Get(fldidx)) {
            auto* cfld = &rtab.Fields_[fldidx];
            FieldToC(*cfld, *fld);
            cfld->Offset_ += static_cast<int32_t>(sizeof(SeedRec)); // = SeedRec::DyMemPos_;
            cfld->InternalOwner_ = fld;
            ++fldidx;
         }
      }
   }
   this->Layout_ = std::move(layout);
}
//--------------------------------------------------------------------------//
static f9sv_SubrIndex LoadSubrIndex(DcQueue& rxbuf) {
   f9sv_SubrIndex subrIndex{kSubrIndexNull};
   BitvTo(rxbuf, subrIndex);
   return subrIndex;
}
RxSubrData::RxSubrData(f9rc_ClientSession& ses, SvFunc fcAck, TreeLocker&& maplk, DcQueue& rxbuf)
   : Session_(ses)
   , NotifyKind_{GetSvFuncSubscribeDataNotifyKind(fcAck)}
   , SubrIndex_{LoadSubrIndex(rxbuf)}
   , SubrRec_{maplk->SubrMap_.GetObjPtr(SubrIndex_)}
   , SeedRec_{SubrRec_ ? SubrRec_->Seeds_[SubrRec_->TabIndex_].get() : nullptr}
   , SubrTab_{SubrRec_ ? SubrRec_->Tree_->Layout_->GetTab(SubrRec_->TabIndex_) : nullptr}
   , LockedMap_{std::move(maplk)}
   , IsNeedsLog_(f9rc_ClientLogFlag{} != (ses.LogFlags_ & f9sv_ClientLogFlag_SubscribeData)
                 && LogLevel::Info >= LogLevel_)
   , IsSubrTree_{SubrRec_ && seed::IsSubrTree(SubrRec_->SeedKey_.begin())} {
   if (this->IsNeedsLog_) {
      RevPutChar(this->LogBuf_, '\n');
      if (this->SubrRec_ == nullptr)
         RevPrint(this->LogBuf_, "|err=Bad subrIndex.");
   }
   assert(this->SeedRec_ == nullptr || (this->SeedRec_->SubrIndex_ == this->SubrIndex_));
}
RxSubrData::RxSubrData(RxSubrData& src)
   : Session_(src.Session_)
   , NotifyKind_{src.NotifyKind_}
   , SubrIndex_{src.SubrIndex_}
   , SubrRec_{src.SubrRec_}
   , SeedRec_{src.SeedRec_}
   , SubrTab_{src.SubrTab_}
   , IsNeedsLog_{src.IsNeedsLog_}
   , IsSubrTree_{src.IsSubrTree_} {
   if (this->IsNeedsLog_)
      RevPutChar(this->LogBuf_, '\n');
}
void RxSubrData::FlushLog() {
   if (!this->IsNeedsLog_)
      return;
   this->LogSubrRec();
   this->IsNeedsLog_ = false;
   RevPrint(this->LogBuf_, "f9sv_SubscribeData"
            "|subrIndex=", this->SubrIndex_,
            "|notifyKind=", this->NotifyKind_);
   LogWrite(LogLevel::Info, std::move(this->LogBuf_));
}
void RxSubrData::LogSubrRec() {
   if (!this->IsNeedsLog_ || this->SubrRec_ == nullptr || this->IsSubrLogged_)
      return;
   this->IsSubrLogged_ = true;
   if (this->IsSubrTree_)
      RevPrint(this->LogBuf_, "|rxSeedKey=", this->SeedKey_);
   RevPrint(this->LogBuf_,
            "|treePath=", this->SubrRec_->Tree_->TreePath_,
            "|seedKey=", this->SubrRec_->SeedKey_,
            "|tab=", this->SubrRec_->TabIndex_,
            ':', this->SubrTab_->Name_);
}
StrView RxSubrData::CheckLoadSeedKey(DcQueue& rxbuf) {
   if (this->IsSubrTree_)
      BitvTo(rxbuf, this->SeedKey_);
   else
      this->SeedKey_ = this->SubrRec_->SeedKey_;
   return ToStrView(this->SeedKey_);
}
void RxSubrData::LoadGv(DcQueue& rxbuf) {
   BitvTo(rxbuf, this->Gv_);
   if (this->IsNeedsLog_)
      RevPrint(this->LogBuf_, "|gv={", this->Gv_, '}');
}
void RxSubrData::RemoveSubscribe() {
   this->LogSubrRec();
   this->SubrRec_->ClearSvFuncSubr();
   this->SeedRec_->ClearSubscriber();
   this->LockedMap_->SubrMap_.RemoveObjPtr(this->SubrIndex_, this->SubrRec_);
}

} } } // namespaces
