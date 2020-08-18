﻿/// \file fon9/framework/IoManager.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_framework_IoManager_hpp__
#define __fon9_framework_IoManager_hpp__
#include "fon9/framework/IoFactory.hpp"

#ifdef fon9_WINDOWS
#include "fon9/io/win/IocpService.hpp"
#else
#include "fon9/io/FdrService.hpp"
#endif

namespace fon9 {

fon9_WARN_DISABLE_PADDING;
struct fon9_API IoManagerArgs {
   std::string Name_;
   
   /// 設定檔, 如果為空, 表示不使用設定檔.
   std::string CfgFileName_;

   /// 若 IoServiceSrc_ != nullptr 則不理會 IoServiceCfgstr_;
   /// 直接與 IoServiceSrc_ 共用 io service.
   IoManagerSP IoServiceSrc_;
   std::string IoServiceCfgstr_;
   
   /// 如果 SessionFactoryPark_ == nullptr, 則 IoManager 會自己建立一個.
   SessionFactoryParkSP SessionFactoryPark_;
   /// 如果 DeviceFactoryPark_ == nullptr, 則 IoManager 會自己建立一個.
   DeviceFactoryParkSP  DeviceFactoryPark_;

   /// 提供 IoManager 建構結果, 例如: 提供 ConfigFileBinder_.OpenRead(...args.CfgFileName_); 的錯誤訊息.
   mutable std::string Result_;

   IoManagerArgs() = default;
   IoManagerArgs(std::string name) : Name_{std::move(name)} {
   }

   /// - 移除 iosvCfg 的括號之後, 然後判斷首碼.
   /// - 若 iosvCfg.Get1st()=='/'
   ///   - 則從 holder.Root_ 尋找 IoManager 填入 this->IoServiceSrc_;
   ///   - 例如: iosvCfg = "/MaIo";
   ///   - 若沒找到 IoManager, 則:
   ///     - holder.SetPluginsSt(LogLevel::Error, this->Name_, ".SetIoService|err=", ...);
   ///     - 返回 false;
   /// - 若 iosvCfg.Get1st()!='/'; 例如: "ThreadCount=2|Capacity=100"
   ///   - this->IoServiceCfgstr_ = iosvCfg.ToString();
   ///   - this->IoServiceSrc_.reset();
   bool SetIoServiceCfg(seed::PluginsHolder& holder, StrView iosvCfg);

   /// tag                | value
   /// -------------------|----------------------------
   /// Name               | this->Name_ = value;
   ///                    |
   /// DeviceFactoryPark  |
   /// DevFp              | this->DeviceFactoryPark_ = seed::FetchNamedPark<DeviceFactoryPark>(*holder.Root_, value);
   ///                    |
   /// SessionFactoryPark |
   /// SesFp              | this->SessionFactoryPark_ = seed::FetchNamedPark<SessionFactoryPark>(*holder.Root_, value);
   ///                    |
   /// IoService          |
   /// Svc                | iomArgs.IoServiceSrc_ = holder.Root_->GetSapling<IoManager>(fld);
   ///                    |
   /// IoServiceCfg       | 例: "ThreadCount=2|Capacity=100"
   /// SvcCfg             | this->SetIoServiceCfg(holder, value);
   ///                    |
   /// Config             |
   /// Cfg                | this->CfgFileName_ = SysEnv_GetConfigPath(*holder.Root_).ToString() + value;
   ///
   /// \retval false 不認識的 tag.
   bool SetTagValue(seed::PluginsHolder& holder, StrView tag, StrView value);
};

class fon9_API IoManager : public io::Manager {
   fon9_NON_COPY_NON_MOVE(IoManager);

public:
   const std::string          Name_;
   const SessionFactoryParkSP SessionFactoryPark_;
   const DeviceFactoryParkSP  DeviceFactoryPark_;

   IoManager(const IoManagerArgs& args);

   /// 銷毀全部的 devices, 但是不清除 DeviceMap, 所以設定還在RAM, 可以重新開啟.
   void DisposeDevices(std::string cause) {
      this->LockedDisposeDevices(DeviceMap::Locker{this->DeviceMap_}, std::move(cause));
   }

   /// 若 id 重複, 則返回 false, 表示失敗.
   /// 若 cfg.Enabled_ == EnabledYN::Yes 則會啟用該設定.
   /// - 若有 bind config file, 透過這裡加入設定, 不會觸發更新設定檔.
   ///   在透過 TreeOp 增刪改, 才會觸發寫入設定檔.
   bool AddConfig(StrView id, const IoConfigItem& cfg);

   void OnSession_StateUpdated(io::Device& dev, StrView stmsg, LogLevel lv) override;

   /// 在首次有需要時(GetIocpService() or GetFdrService()), 才會將該 io service 建立起來.
#ifdef __fon9_io_win_IocpService_hpp__
   private:
      io::IocpServiceSP IocpService_;
      // Windows 也可能會有 FdrService_? 或其他種類的 io service?
   public:
      io::IocpServiceSP GetIocpService();
      io::IocpServiceSP GetIoService() {
         return this->GetIocpService();
      }
#endif

#ifdef __fon9_io_FdrService_hpp__
   private:
      io::FdrServiceSP  FdrService_;
      // Linux/Unix 可能會有其他種類的 io service?
   public:
      io::FdrServiceSP GetFdrService();
      io::FdrServiceSP GetIoService() {
         return this->GetFdrService();
      }
#endif


protected:
   const std::string IoServiceCfgstr_;

   struct DeviceRun {
      io::DeviceSP   Device_;
      CharVector     SessionSt_;
      CharVector     DeviceSt_;
      seed::TreeSP   Sapling_;
      CharVector     OpenArgs_;

      /// - 如果此值==0 表示:
      ///   - this->Device_ 是從 DeviceItem 設定建立而來.
      ///   - 若 this->Device_ 為 DeviceServer, 則 this->Sapling_ 為 AcceptedClients tree.
      /// - 如果此值!=0 表示:
      ///   - this->Device_ 是 Accepted Client.
      ///   - this->Sapling_ 為顯示此 AcceptedClients 的 tree.
      io::DeviceAcceptedClientSeq   AcceptedClientSeq_{0};
      bool IsDeviceItem() const {
         return this->AcceptedClientSeq_ == 0;
      }
   };
   static DeviceRun* FromManagerBookmark(io::Device& dev);

   using DeviceItemId = CharVector;
   struct DeviceItem : public DeviceRun, public intrusive_ref_counter<DeviceItem> {
      SchSt          SchSt_{SchSt::Unknown};
      SchConfig      Sch_;
      DeviceItemId   Id_;
      IoConfigItem   Config_;

      DeviceItem(StrView id) : Id_{id} {
      }
      DeviceItem(StrView id, const IoConfigItem& cfg) : Id_{id}, Config_(cfg) {
      }
      /// this->SchSt_ = SchSt::Unknown;
      /// \retval true  this->Device_ != nullptr; 也就是有呼叫 this->Device_->AsyncDispose();
      /// \retval false 清除 this->SessionSt_; 設定 this->DeviceSt_ = now + cause;
      bool AsyncDisposeDevice(TimeStamp now, StrView cause);
   };
   using DeviceItemSP = intrusive_ptr<DeviceItem>;

   struct CmpDeviceItemSP {
      bool operator()(const DeviceItemSP& lhs, const DeviceItemSP& rhs) const { return lhs->Id_ < rhs->Id_; }
      bool operator()(const DeviceItemSP& lhs, const StrView& rhs) const { return ToStrView(lhs->Id_) < rhs; }
      bool operator()(const StrView& lhs, const DeviceItemSP& rhs) const { return lhs < ToStrView(rhs->Id_); }
      bool operator()(const DeviceItemId& lhs, const DeviceItemSP& rhs) const { return lhs < rhs->Id_; }
      bool operator()(const DeviceItemSP& lhs, const DeviceItemId& rhs) const { return lhs->Id_ < rhs; }
   };
   using DeviceMapImpl = SortedVectorSet<DeviceItemSP, CmpDeviceItemSP>;
   // 在 IoManagerTree 的 opTree 有提供訂閱, 在訂閱成功時, 可能會需要取得 gv, 因此需要使用 recursive_mutex;
   using DeviceMap = MustLock<DeviceMapImpl, std::recursive_mutex>;
   DeviceMap   DeviceMap_;

   template <class IoService>
   intrusive_ptr<IoService> MakeIoService() const;

   void RaiseMakeIoServiceFatal(Result2 err) const;
   void ParseIoServiceArgs(io::IoServiceArgs& iosvArgs) const;

   void OnDevice_Accepted(io::DeviceServer& server, io::DeviceAcceptedClient& client) override;
   void OnDevice_Initialized(io::Device& dev) override;
   void OnDevice_Destructing(io::Device& dev) override;
   void OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) override;
   void OnDevice_StateUpdated(io::Device& dev, const io::StateUpdatedArgs& e) override;
   void UpdateDeviceState(io::Device& dev, const io::StateUpdatedArgs& e);
   void UpdateDeviceStateLocked(io::Device& dev, const io::StateUpdatedArgs& e);
   void UpdateSessionStateLocked(io::Device& dev, StrView stmsg, LogLevel lv);

   enum class DeviceOpenResult {
      NewDeviceCreated = 0,
      AlreadyExists,
      Disabled,
      SessionFactoryNotFound,
      DeviceFactoryNotFound,
      DeviceCreateError,
   };
   DeviceOpenResult CheckOpenDevice(DeviceItem& item);
   DeviceOpenResult CheckCreateDevice(DeviceItem& item);
   DeviceOpenResult CreateDevice(DeviceItem& item);

   static void AssignStStr(CharVector& dst, TimeStamp now, StrView stmsg);
   virtual void NotifyChanged(DeviceItem&) = 0;
   virtual void NotifyChanged(DeviceRun&) = 0;

   struct AcceptedTree;
   static seed::LayoutSP MakeAcceptedClientLayoutImpl();
   static seed::LayoutSP GetAcceptedClientLayout();
   static void MakeAcceptedClientTree(DeviceRun& serverItem, io::DeviceListenerSP listener);

   void LockedDisposeDevices(const DeviceMap::Locker& map, std::string cause);
};
fon9_WARN_POP;

} // namespaces
#endif//__fon9_framework_IoManager_hpp__
