﻿/// \file fon9/MustLock.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_MustLock_hpp__
#define __fon9_MustLock_hpp__
#include "fon9/Utility.hpp"
fon9_BEFORE_INCLUDE_STD;
#include <mutex>
fon9_AFTER_INCLUDE_STD;

namespace fon9 {

fon9_WARN_DISABLE_PADDING;
/// \ingroup Thrs
/// 必須先鎖定, 才能使用 BaseT.
template <class BaseT,
   class MutexT = std::mutex,
   class MutexLockerT = std::unique_lock<MutexT>>
class MustLock {
   fon9_NON_COPY_NON_MOVE(MustLock);
protected:
   // 如果使用特殊自訂的 Mutex_, 則也許在建構之後要額外設定,
   // 例如: Mutex_ 需參考 Base_ 的內容才能進行 lock()/unlock();
   // 所以使用 protected; 讓衍生者有機會調整 Mutex_;
   MutexT mutable Mutex_;
   BaseT          Base_;

   #ifndef NDEBUG
      mutable std::thread::id   LockedThread_{};
      mutable size_t            LockedCount_{};
      void AfterLocked() const {
         this->LockedThread_ = std::this_thread::get_id();
         ++this->LockedCount_;
      }
      void BeforeUnlocked() const {
         if(--this->LockedCount_ <= 0)
            this->LockedThread_ = std::thread::id{};
      }
   #else
      void AfterLocked() const {
      }
      void BeforeUnlocked() const {
      }
   #endif
public:
   using MutexLocker = MutexLockerT;

   template <class... ArgsT>
   MustLock(ArgsT&&... args) : Base_{std::forward<ArgsT>(args)...} {
   }

   /// 必須透過鎖定才能取得 LockedBaseT 物件.
   template <class OwnerT, class LockedBaseT>
   class LockerT : public MutexLockerT {
      fon9_NON_COPYABLE(LockerT);
      using base = MutexLockerT;
      OwnerT*  Owner_;
   public:
      LockerT() : Owner_{nullptr} {
      }
      LockerT(OwnerT& owner) : base{owner.Mutex_}, Owner_{&owner} {
         owner.AfterLocked();
      }
      LockerT(LockerT&& other) : base{std::move(other)}, Owner_{other.Owner_} {
         other.Owner_ = nullptr;
      }
      LockerT& operator=(LockerT&& other) {
         base::operator=(std::move(other));
         this->Owner_ = other.Owner_;
         other.Owner_ = nullptr;
         return *this;
      }
      ~LockerT() {
         if (this->Owner_)
            this->Owner_->BeforeUnlocked();
      }
      /// 必須在鎖定狀態下才能呼叫, 否則資料可能會被破壞, 甚至造成 crash!
      LockedBaseT* operator->() const {
         assert(this->owns_lock());
         return &this->Owner_->Base_;
      }
      /// 必須在鎖定狀態下才能呼叫, 否則資料可能會被破壞, 甚至造成 crash!
      LockedBaseT& operator*() const {
         assert(this->owns_lock());
         return this->Owner_->Base_;
      }
      /// 取出的物件必須小心使用, 如果您呼叫了 unlock(); 則在 lock() 之前, 都不可使用他.
      LockedBaseT* get() const { return &this->Owner_->Base_; }
      OwnerT* GetOwner() const { return this->Owner_; }
      /// 若 this->owns_lock() 則 do nothing.
      /// 若 !this->owns_lock() 則重新上鎖.
      void Relock(OwnerT& owner) {
         assert(this->Owner_ == nullptr || this->Owner_ == &owner);
         if (this->owns_lock())
            return;
         this->lock();
         this->Owner_ = &owner;
      }
   };
   using Locker = LockerT<MustLock, BaseT>;
   using ConstLocker = LockerT<const MustLock, const BaseT>;

   Locker Lock() { return Locker{*this}; }
   ConstLocker Lock() const { return ConstLocker{*this}; }
   ConstLocker ConstLock() const { return ConstLocker{*this}; }

   static MustLock& StaticCast(BaseT& pbase) {
      return ContainerOf(pbase, &MustLock::Base_);
   }
   static const MustLock& StaticCast(const BaseT& pbase) {
      return ContainerOf(*const_cast<BaseT*>(&pbase), &MustLock::Base_);
   }

   #ifndef NDEBUG
   bool IsLocked() const {
      return this->LockedThread_ == std::this_thread::get_id()
         && this->LockedCount_ > 0;
   }
   #endif
};
fon9_WARN_POP;

} // namespaces
#endif//__fon9_MustLock_hpp__
