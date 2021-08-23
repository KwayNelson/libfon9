﻿// \file fon9/seed/SeedVisitor.cpp
// \author fonwinz@gmail.com
#include "fon9/seed/SeedVisitor.hpp"
#include "fon9/seed/TabTreeOp.hpp"
#include "fon9/buffer/DcQueueList.hpp"
#include "fon9/Log.hpp"

namespace fon9 { namespace seed {

/// 可以考慮透過設定(或Op操作)改變此變數.
fon9_API LogLevel VisitorLogLevel = LogLevel::Info;

static void TicketLogRequest(StrView opName, TicketRunner& ticket, const Tab* tab, StrView cmd) {
   if (fon9_UNLIKELY(VisitorLogLevel < LogLevel_))
      return;
   RevBufferList rbuf{kLogBlockNodeSize};
   RevPutChar(rbuf, '\n');
   if (!cmd.IsNull())
      RevPrint(rbuf, "|cmd=", cmd);
   if (tab)
      RevPrint(rbuf, "|tab=", tab->Name_);
   RevPrint(rbuf, opName,
            "|ticket=", ToPtr(&ticket),
            '|', ticket.Visitor_->GetUFrom(),
            "|path=", ticket.OrigPath_);
   LogWrite(VisitorLogLevel, std::move(rbuf));
}
static void TicketLogResult(StrView opName, TicketRunner& ticket, OpResult opResult, StrView msg) {
   if (fon9_UNLIKELY(VisitorLogLevel < LogLevel_))
      return;
   RevBufferList rbuf{kLogBlockNodeSize};
   RevPutChar(rbuf, '\n');
   if (!msg.IsNull())
      RevPrint(rbuf, "|msg=", msg);
   RevPrint(rbuf, opName,
            "|ticket=", ToPtr(&ticket),
            "|res=", GetOpResultMessage(opResult));
   LogWrite(VisitorLogLevel, std::move(rbuf));
}

//--------------------------------------------------------------------------//
SeedVisitor::SeedVisitor(MaTreeSP root, std::string ufrom, const auth::AuthResult& authr)
   : UFrom_{std::move(ufrom)}
   , Fairy_{new SeedFairy{std::move(root)}}
   , AuthR_(authr) {
}
SeedVisitor::~SeedVisitor() {
}
void SeedVisitor::GetUserAndFrom(StrView& user, StrView& from) const {
   StrView vfrom = ToStrView(this->GetUFrom());
   StrView tag, value;
   while (StrFetchTagValue(vfrom, tag, value)) {
      if (tag == "U")
         user = value;
      else if (tag == "R")
         from = StrFetchNoTrim(value, ':');
   }
}
void SeedVisitor::SetCurrPath(StrView currPath) {
   this->Fairy_->SetCurrPath(currPath);
}
void SeedVisitor::OnTicketRunnerDone(TicketRunner& runner, DcQueue&& extmsg) {
   (void)runner; (void)extmsg;
}
void SeedVisitor::OnTicketRunnerWrite(TicketRunnerWrite& runner, const SeedOpResult& res, const RawWr& wr) {
   (void)runner; (void)res; (void)wr;
}
void SeedVisitor::OnTicketRunnerRead(TicketRunnerRead& runner, const SeedOpResult& res, const RawRd& rd) {
   (void)runner; (void)res; (void)rd;
}
void SeedVisitor::OnTicketRunnerRemoved(TicketRunnerRemove& runner, const PodRemoveResult& res) {
   (void)runner; (void)res;
}
void SeedVisitor::OnTicketRunnerGridView(TicketRunnerGridView& runner, GridViewResult& res) {
   (void)runner; (void)res;
}
bool SeedVisitor::OnTicketRunnerBeforeGridView(TicketRunnerGridView& runner, TreeOp& opTree, GridViewRequest& req) {
   (void)runner; (void)opTree; (void)req;
   return true;
}
void SeedVisitor::OnTicketRunnerCommand(TicketRunnerCommand& runner, const SeedOpResult& res, StrView msg) {
   (void)runner; (void)res; (void)msg;
}
void SeedVisitor::OnTicketRunnerSetCurrPath(TicketRunnerCommand& runner) {
   (void)runner;
}
void SeedVisitor::OnSeedNotify(VisitorSubr& subr, const SeedNotifyArgs& args) {
   (void)subr; (void)args;
}
void SeedVisitor::OnTicketRunnerSubscribe(TicketRunnerSubscribe& runner, bool isSubOrUnsub) {
   (void)runner; (void)isSubOrUnsub;
}

//--------------------------------------------------------------------------//
void TicketRunnerError::OnFoundTree(TreeOp&) {
}
void TicketRunnerError::OnLastSeedOp(const PodOpResult&, PodOp*, Tab&) {
}
TicketRunnerSP TicketRunnerError::ArgumentsMustBeEmpty(SeedVisitor& visitor) {
   return new TicketRunnerError(visitor, OpResult::bad_command_argument, "Arguments must be empty.");
}
TicketRunnerSP TicketRunnerError::UnknownCommand(SeedVisitor& visitor) {
   return new TicketRunnerError(visitor, OpResult::not_supported_cmd, "Unknown command.");
}
//--------------------------------------------------------------------------//
void TicketRunnerTree::OnLastStep(TreeOp& opTree, StrView keyText, Tab& tab) {
   this->ContinueSeed(opTree, keyText, tab); // 進入 OnFoundTree();
}
void TicketRunnerTree::OnLastSeedOp(const PodOpResult&, PodOp*, Tab&) {
}
//--------------------------------------------------------------------------//
void TicketRunnerRead::OnFoundTree(TreeOp&) {
   this->OnError(OpResult::not_supported_read);
}
void TicketRunnerRead::OnLastSeedOp(const PodOpResult& resPod, PodOp* pod, Tab& tab) {
   if (pod)
      pod->BeginRead(tab, std::bind(&TicketRunnerRead::OnReadOp,
                                    intrusive_ptr<TicketRunnerRead>(this),
                                    std::placeholders::_1,
                                    std::placeholders::_2));
   else
      this->OnError(resPod.OpResult_);
}
void TicketRunnerRead::OnReadOp(const SeedOpResult& res, const RawRd* rd) {
   if (rd)
      this->Visitor_->OnTicketRunnerRead(*this, res, *rd);
   else
      this->OnError(res.OpResult_);
}
//--------------------------------------------------------------------------//
void TicketRunnerWrite::OnFoundTree(TreeOp&) {
   this->OnError(OpResult::not_supported_write);
}
void TicketRunnerWrite::OnLastStep(TreeOp& op, StrView keyText, Tab& tab) {
   this->RemainPath_.SetBegin(keyText.begin());
   if (IsEnumContains(this->Rights_, AccessRight::Insert))
      op.Add(keyText, std::bind(&TicketRunnerWrite::OnLastSeedOp,
                                intrusive_ptr<TicketRunnerWrite>(this),
                                std::placeholders::_1,
                                std::placeholders::_2,
                                std::ref(tab)));
   else
      base::OnLastStep(op, keyText, tab);
}
void TicketRunnerWrite::OnLastSeedOp(const PodOpResult& resPod, PodOp* pod, Tab& tab) {
   if (pod)
      pod->BeginWrite(tab, std::bind(&TicketRunnerWrite::OnWriteOp,
                                     intrusive_ptr<TicketRunnerWrite>(this),
                                     std::placeholders::_1,
                                     std::placeholders::_2));
   else
      this->OnError(resPod.OpResult_);
}
void TicketRunnerWrite::OnWriteOp(const SeedOpResult& res, const RawWr* wr) {
   if (wr)
      this->Visitor_->OnTicketRunnerWrite(*this, res, *wr);
   else
      this->OnError(res.OpResult_);
}
void TicketRunnerWrite::ParseSetValues(RevBufferList& rbuf, const SeedOpResult& res, const RawWr& wr, const char chSpl) {
   StrView  fldvals{&this->FieldValues_};
   TicketLogRequest("SeedOp.Write", *this, res.Tab_, fldvals);
   while (!fldvals.empty()) {
      StrView val = SbrFetchNoTrim(fldvals, chSpl);
      StrView fldName = StrFetchTrim(val, '=');
      auto    fld = res.Tab_->Fields_.Get(fldName);
      if (fld == nullptr)
         RevPrint(rbuf, "fieldName=", fldName, "|err=field not found\n");
      else {
         val = StrNoTrimRemoveQuotes(val);
         // 把 Non-breaking space: 0xc2 0xa0 改成 ' ';
         std::string nstr;
         nstr.reserve(val.size());
         auto iend = val.end();
         for (auto i = val.begin(); i != iend; ++i) {
            __CONTINUE_SAME_I:
            if (fon9_UNLIKELY(*i == '\xc2')) {
               if (++i == iend) {
                  nstr.push_back('\xc2');
                  break;
               }
               if (*i == '\xa0') {
                  nstr.push_back(' ');
                  continue;
               }
               nstr.push_back('\xc2');
               goto __CONTINUE_SAME_I;
            }
            nstr.push_back(*i);
         }
         seed::OpResult r = fld->StrToCell(wr, &nstr);
         if (r != seed::OpResult::no_error)
            RevPrint(rbuf, "fieldName=", fldName, "|err=StrToCell():", r, ':', seed::GetOpResultMessage(r), '\n');
      }
   }
}
//--------------------------------------------------------------------------//
TicketRunnerRemove::TicketRunnerRemove(SeedVisitor& visitor, StrView seed)
   : base(visitor, seed, AccessRight::Remove) {
   this->RemovedHandler_ = std::bind(&TicketRunnerRemove::OnRemoved, this, std::placeholders::_1);
}
void TicketRunnerRemove::OnFoundTree(TreeOp&) {
   this->OnError(OpResult::not_supported_remove_pod);
}
void TicketRunnerRemove::OnLastSeedOp(const PodOpResult&, PodOp*, Tab&) {
   // 不會執行到這兒.
   this->OnError(OpResult::not_supported_remove_pod);
}
void TicketRunnerRemove::ContinuePod(TreeOp& opTree, StrView keyText, StrView tabName) {
   this->ContinuePodForRemove(opTree, keyText, tabName, this->RemovedHandler_);
}
void TicketRunnerRemove::OnRemoved(const PodRemoveResult& res) {
   this->OpResult_ = res.OpResult_;
   this->Visitor_->OnTicketRunnerRemoved(*this, res);
}
void TicketRunnerRemove::OnBeforeRemove(TreeOp& opTree, StrView keyText, Tab* tab) {
   (void)opTree;  (void)keyText; (void)tab;
   TicketLogRequest("SeedOp.Remove.Bf", *this, nullptr, nullptr);
}
void TicketRunnerRemove::OnAfterRemove(const PodRemoveResult& res) {
   TicketLogResult("SeedOp.Remove.Af", *this, res.OpResult_, nullptr);
}
//--------------------------------------------------------------------------//
TicketRunnerGridView::TicketRunnerGridView(SeedVisitor& visitor, StrView seed, ReqMaxRowCountT reqMaxRowCount, StrView startKey, StrView tabName)
   : base{visitor, seed}
   , StartKeyBuf_{startKey}
   , StartKey_{startKey}
   , TabName_{tabName}
   , ReqMaxRowCount_{reqMaxRowCount} {
   if (!IsTextBeginOrEnd(startKey))
      this->StartKey_ = ToStrView(this->StartKeyBuf_);
}
void TicketRunnerGridView::Continue() {
   this->RemainPath_ = ToStrView(this->OrigPath_);
   this->StartKeyBuf_ = this->LastKey_;
   this->StartKey_ = ToStrView(this->StartKeyBuf_);
   if (this->ReqMaxRowCount_ < 0)
      this->ReqMaxRowCount_ = static_cast<ReqMaxRowCountT>(-this->ReqMaxRowCount_);
}
void TicketRunnerGridView::OnFoundTree(TreeOp& opTree) {
   GridViewRequest req{this->StartKey_};
   if (this->ReqMaxRowCount_ >= 0)
      req.MaxRowCount_ = unsigned_cast(this->ReqMaxRowCount_);
   else {
      req.Offset_ = this->ReqMaxRowCount_;
      if (!IsTextEnd(this->StartKey_))
         ++req.Offset_;
      req.MaxRowCount_ = unsigned_cast(static_cast<ReqMaxRowCountT>(-this->ReqMaxRowCount_));
   }
   // 如果有要求 RowCount (int16_t, uint16_t), 則 BufferSize 不限制.
   // this->ReqMaxRowCount_ 最多 37K, req.MaxRowCount_ 最多 64K.
   // 所以 MaxBufferSize_ 不用再限制 (如果每筆 100B, 最多約 3M).
   if (req.MaxRowCount_)
      req.MaxBufferSize_ = 0;
   req.Tab_ = opTree.Tree_.LayoutSP_->GetTabByNameOrFirst(ToStrView(this->TabName_));
   if (req.Tab_ == nullptr)
      this->OnError(OpResult::not_found_tab);
   else
      this->OnFoundTab(opTree, req);
}
void TicketRunnerGridView::OnFoundTab(TreeOp& opTree, GridViewRequest& req) {
   if (this->Visitor_->OnTicketRunnerBeforeGridView(*this, opTree, req)) {
      opTree.GridView(req, std::bind(&TicketRunnerGridView::OnGridViewOp,
                                     intrusive_ptr<TicketRunnerGridView>(this),
                                     std::placeholders::_1));
   }
}
void TicketRunnerGridView::OnGridViewOp(GridViewResult& res) {
   if (res.OpResult_ != OpResult::no_error)
      this->OnError(res.OpResult_);
   else {
      this->LastKey_.assign(res.GetLastKey());
      this->Visitor_->OnTicketRunnerGridView(*this, res);
   }
}
//--------------------------------------------------------------------------//
// /xxx/^edit:tabName  需要有 AccessRight::Write 權限
// /xxx/^apply:tabName 需要有 AccessRight::Apply 權限
static AccessRight CheckCommandRight(StrView seed) {
   if (const char* pspl = StrRFindIf(seed, [](unsigned char ch) { return ch == '/'; })) {
      seed.SetBegin(pspl + 1);
      if (seed.Get1st() == '^') {
         seed.SetBegin(pspl + 2);
         if ((pspl = seed.Find(':')) != nullptr) {
            seed.SetEnd(pspl);
            if(seed == kTabTree_KeyEdit)
               return AccessRight::Write;
            if (seed == kTabTree_KeyApply)
               return AccessRight::Apply;
         }
      }
   }
   return AccessRight::Exec;
}
// 如果 cmdln.empty() 則表示切換現在路徑: this->Visitor_->SetCurrPath(ToStrView(this->Path_));
TicketRunnerCommand::TicketRunnerCommand(SeedVisitor& visitor, StrView seed, std::string&& cmdln)
   : base(visitor, seed, cmdln.empty() ? AccessRight::None : CheckCommandRight(seed))
   , SeedCommandLine_{std::move(cmdln)} {
}
void TicketRunnerCommand::SetNewCurrPath() {
   this->Visitor_->OnTicketRunnerSetCurrPath(*this);
}
void TicketRunnerCommand::OnFoundTree(TreeOp&) {
   if (this->SeedCommandLine_.empty())
      this->SetNewCurrPath();
   else
      this->OnError(OpResult::not_supported_cmd);
}
void TicketRunnerCommand::OnLastSeedOp(const PodOpResult& resPod, PodOp* pod, Tab& tab) {
   if (!pod)
      this->OnError(resPod.OpResult_);
   else if (this->SeedCommandLine_.empty())
      this->SetNewCurrPath();
   else {
      if (this->SeedCommandLine_ != "?")
         TicketLogRequest("SeedOp.Command", *this, &tab, ToStrView(this->SeedCommandLine_));
      pod->OnVisitorCommand(&tab, &this->SeedCommandLine_,
                            std::bind(&TicketRunnerCommand::OnSeedCommandResult,
                                      intrusive_ptr<TicketRunnerCommand>(this),
                                      std::placeholders::_1,
                                      std::placeholders::_2),
                            *this->Visitor_);
   }
}
void TicketRunnerCommand::OnSeedCommandResult(const SeedOpResult& res, StrView msg) {
   this->OpResult_ = res.OpResult_;
   this->Visitor_->OnTicketRunnerCommand(*this, res, msg);
   if (this->SeedCommandLine_ != "?")
      TicketLogResult("SeedOp.Command", *this, res.OpResult_, msg);
}
//--------------------------------------------------------------------------//
TicketRunnerSubscribe::TicketRunnerSubscribe(SeedVisitor& visitor, StrView seed, StrView tabName)
   : base{visitor, seed}
   , TabName_{tabName}
   , Subr_{visitor.NewSubscribe()} {
}
#define fon9_kCSTR_UNSUB_TAB_NAME   "<u>"
TicketRunnerSubscribe::TicketRunnerSubscribe(SeedVisitor& visitor, StrView seed, VisitorSubrSP subr)
   : base{visitor, seed}
   , TabName_{StrView{fon9_kCSTR_UNSUB_TAB_NAME}}
   , Subr_{std::move(subr)} {
}
void TicketRunnerSubscribe::OnFoundTree(TreeOp& opTree) {
   if (ToStrView(this->TabName_) == fon9_kCSTR_UNSUB_TAB_NAME) {
      assert(this->Subr_);
      if (this->Visitor_->Subr_.Lock()->get() == this->Subr_.get())
         this->Visitor_->Unsubscribe();
      else
         this->Subr_->Unsubscribe();
      this->Visitor_->OnTicketRunnerSubscribe(*this, false);
   }
   else if(Tab* tab = opTree.Tree_.LayoutSP_->GetTabByNameOrFirst(ToStrView(this->TabName_))) {
      OpResult res = this->Subr_->Subscribe(ToStrView(this->OrigPath_), *tab, opTree);
      if (res != OpResult::no_error)
         this->OnError(res);
      else {
         this->Visitor_->OnTicketRunnerSubscribe(*this, true);
         if (this->Visitor_->Subr_.Lock()->get() != this->Subr_.get())
            this->Subr_->Unsubscribe();
      }
   }
   else
      this->OnError(OpResult::not_found_tab);
}

OpResult VisitorSubr::Subscribe(StrView path, Tab& tab, TreeOp& opTree) {
   assert(this->SubConn_ == nullptr && this->Tree_.get() == nullptr);
   if (this->SubConn_ || this->Tree_)
      return OpResult::not_supported_cmd;
   this->Tab_ = &tab;
   this->Path_.assign(path);
   this->Tree_.reset(&opTree.Tree_);
   return opTree.Subscribe(&this->SubConn_, tab,
                           std::bind(&VisitorSubr::OnSeedNotify, VisitorSubrSP{this},
                                     std::placeholders::_1));
}
void VisitorSubr::OnSeedNotify(const SeedNotifyArgs& args) {
   this->Visitor_->OnSeedNotify(*this, args);
}
void VisitorSubr::Unsubscribe() {
   if (auto tree{std::move(this->Tree_)}) {
      Tab*    tab = this->Tab_;
      SubConn subConn = this->SubConn_;
      this->SubConn_ = nullptr;
      tree->OnTreeOp([subConn, tab](const TreeOpResult&, TreeOp* op) {
         if (op)
            op->UnsubscribeUnsafe(subConn, *tab);
      });
   }
}

void SeedVisitor::Unsubscribe() {
   if (auto subr{std::move(*this->Subr_.Lock())})
      subr->Unsubscribe();
}
VisitorSubrSP SeedVisitor::NewSubscribe() {
   VisitorSubrSP  newSubr{new VisitorSubr{*this}};
   VisitorSubrSP  oldSubr;
   {
      Subr::Locker   locker{this->Subr_};
      oldSubr = std::move(*locker);
      *locker = newSubr;
   }
   if (oldSubr)
      oldSubr->Unsubscribe();
   return newSubr;
}

} } // namespaces
