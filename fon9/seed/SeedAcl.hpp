﻿/// \file fon9/seed/SeedAcl.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_seed_SeedAcl_hpp__
#define __fon9_seed_SeedAcl_hpp__
#include "fon9/seed/Layout.hpp"
#include "fon9/seed/Tab.hpp"
#include "fon9/CharVector.hpp"
#include "fon9/SortedVector.hpp"
#include "fon9/FlowCounter.hpp"

namespace fon9 { namespace seed {

/// \ingroup seed.
/// 用來存取 AccessList 的 layout.
/// - KeyField  = "Path";
/// - Tab[0]    = "AclRights";
/// - Fields[0] = "Rights";
fon9_API LayoutSP MakeAclTreeLayout();
fon9_API LayoutSP MakeAclTreeLayoutWritable();

/// \ingroup seed
/// Seed 的存取權限旗標.
enum class AccessRight : uint8_t {
   None = 0x00,
   Full = 0xff,

   /// 允許讀取欄位內容 or GridView.
   Read = 0x01,
   /// 允許修改欄位內容.
   Write = 0x02,
   /// 允許執行 OnSeedCommand().
   Exec = 0x04,

   /// 允許新增.
   Insert = 0x10,
   /// 允許移除.
   Remove = 0x20,

   /// 允許訂閱整個 tree? (訂閱數量僅增加 1).
   SubrTree = 0x40 | Read,

   /// 允許在 NeedsApply 的資料表執行 [套用].
   Apply = 0x80,
};
fon9_ENABLE_ENUM_BITWISE_OP(AccessRight);

struct AccessControl {
   AccessRight Rights_;
   char        Padding___[3];
   /// AclPath 設定的 tree, 可以訂閱幾個 seed?
   /// - 0 = 不限制.
   /// - 由 Session 處理.
   /// - tree 必須支援訂閱.
   uint32_t    MaxSubrCount_;

   AccessControl() {
      memset(this, 0, sizeof(*this));
   }
};

using AclPath = CharVector;

template <class AclRecT>
struct AclPathMap : public SortedVector<AclPath, AclRecT> {
   using base = SortedVector<AclPath, AclRecT>;
   using key_type = typename base::key_type;
   using iterator = typename base::iterator;
   using const_iterator = typename base::const_iterator;
   using base::lower_bound;
   iterator lower_bound(StrView strKeyText) { return this->lower_bound(key_type::MakeRef(strKeyText)); }
   const_iterator lower_bound(StrView strKeyText) const { return this->lower_bound(key_type::MakeRef(strKeyText)); }

   using base::find;
   iterator find(StrView strKeyText) { return this->find(key_type::MakeRef(strKeyText)); }
   const_iterator find(StrView strKeyText) const { return this->find(key_type::MakeRef(strKeyText)); }
};

/// \ingroup seed
/// Access control list.
using AccessList = AclPathMap<AccessControl>;

struct AclHomeConfig {
   /// 若有包含 "{UserId}" 則從 AclAgent 取出 PolicyAcl 時,
   /// 會用實際的 UserId 取代 "{UserId}",
   /// e.g. "/home/{UserId}" => "/home/fonwin"
   AclPath           Home_;
   /// 除了 Acl_ 各自設定的訂閱數量之外, 這裡可設定一個「每個 Session 最大可訂閱數量」.
   uint32_t          MaxSubrCount_{0};
   /// 查詢的流量管制.
   FlowCounterArgs   FcQuery_;
   /// 回補的流量管制:
   /// FlowControlCalc.SetFlowControlArgs(FcRecover_.FcCount_ * 1000, FcRecover_.FcTimeMS_);
   FlowCounterArgs   FcRecover_;
   char              Padding____[4];

   AclHomeConfig() {
      this->FcQuery_.Clear();
      this->FcRecover_.Clear();
   }

   void Clear() {
      this->Home_.clear();
      this->MaxSubrCount_ = 0;
      this->FcQuery_.Clear();
      this->FcRecover_.Clear();
   }
};

/// \ingroup seed
struct AclConfig : public AclHomeConfig {
   AccessList  Acl_;

   void Clear() {
      AclHomeConfig::Clear();
      this->Acl_.clear();
   }

   /// - Home_ 設為 "/"
   /// - "/" 及 "/.." 權限設為 AccessRight::Full;
   void SetAdminMode() {
      this->Home_.assign("/");
      this->Acl_.kfetch(AclPath::MakeRef("/", 1)).second.Rights_ = AccessRight::Full;
      this->Acl_.kfetch(AclPath::MakeRef("/..", 3)).second.Rights_ = AccessRight::Full;
   }

   /// 沒任何權限?
   bool IsAccessDeny() const {
      return this->Acl_.empty();
   }
};

fon9_WARN_DISABLE_PADDING;
/// \ingroup seed
/// 正規化 seed path, 並透過正規劃結果尋找 Seed 存取權限設定.
/// 正規化之後: '/' 開頭, 尾端沒有 '/'
struct fon9_API AclPathParser {
   AclPath              Path_;
   std::vector<size_t>  IndexEndPos_;

   /// 正規化之後的路徑存放在 this->Path_;
   /// 若正規化失敗, 則 this 不會變動;
   /// \retval false  path 解析有誤, path.begin()==錯誤的位置.
   bool NormalizePath(StrView& path);

   /// 正規化之後的路徑存放在 this->Path_;
   /// 若正規化失敗, 則 this->Path_ = str; IndexEndPos_.empty()
   /// \retval !=nullptr 若 path 解析有誤, 則傳回錯誤的位置.
   /// \retval ==nullptr 解析正確完成.
   const char* NormalizePathStr(StrView path);

   /// 根據 Path_ 檢查 acl 的設定.
   /// - 必須先執行過 NormalizePath()
   /// - 優先使用完全一致的設定: 從後往前解析, 找到符合的權限設定.
   const AccessControl* GetAccess(const AccessList& acl) const;
   /// 若 needsRights != AccessRight::None,
   /// 則必須要有 needsRights 的完整權限, 才會傳回 acl 所設定的權限.
   const AccessControl* CheckAccess(const AccessList& acl, AccessRight needsRights) const;
};
fon9_WARN_POP;

/// \ingroup seed
/// 正規化 seedPath, 若 seedPath 符合規則, 則傳回正規化之後的字串.
/// 若 seedPath 不符合規則, 則傳回 seedPath.
fon9_API AclPath AclPathNormalize(StrView seedPath);

/// \ingroup seed
/// - 若 seedPath 為 "/.." 或 "/../" 開頭, 則為 VisitorsTree, 傳回移除 "/.." 之後的位置.
/// - 否則不是 VisitorsTree, 傳回 nullptr;
fon9_API const char* IsVisitorsTree(StrView seedPath);

} } // namespaces
#endif//__fon9_seed_SeedAcl_hpp__
