﻿/// \file fon9/io/SendBuffer.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_io_SendBuffer_hpp__
#define __fon9_io_SendBuffer_hpp__
#include "fon9/io/Device.hpp"
#include "fon9/buffer/DcQueueList.hpp"

namespace fon9 { namespace io {

fon9_WARN_DISABLE_PADDING;
/// \ingroup io
/// 傳送緩衝區: 如果情況允許, 則提供 **立即可送** 的返回值, 由呼叫端負責立即送出。
/// - 所有的操作只能在 OpQueue safe 的狀態下進行.
/// - 提供可讓 Overlapped I/O 可用的介面:
///   - `ToSendingAndUnlock()` => `ContinueSend（)`
///   - if (!SendBuffer_.IsEmpty()) 將資料填入 `GetQueueForPush()`
/// - 提供一般消費函式 send(), write()... 使用的介面:
///   - ASAP
///   - Buffered
class SendBuffer {
   fon9_NON_COPY_NON_MOVE(SendBuffer);

   enum class Status {
      Empty,
      Sending,
   };
   Status      Status_{Status::Empty};
   BufferList  Queue_;
   DcQueueList Sending_;

public:
   SendBuffer() = default;

   bool IsEmpty() const {
      return this->Status_ == Status::Empty;
   }

   /// 無保護措施, 因為:
   /// 通常在 IoBuffer 的衍生者或使用者(IocpSocket,FdrSocket)解構時呼叫.
   void ForceClearBuffer(const ErrC& errc) {
      this->Sending_.push_back(std::move(this->Queue_));
      this->Sending_.ConsumeErr(errc);
      this->Status_ = Status::Empty;
   }

   using ALocker = DeviceOpQueue::ALockerForInplace;

   /// 設定進入傳送中的狀態.
   /// \retval nullptr  現在正在傳送中, 設定失敗.
   /// \retval !nullptr 現在沒有在傳送, 成功進入 Sending 狀態:
   ///                  - 傳回值可用於填入資料後立即傳送.
   ///                  - 返回前會先執行 alocker.UnlockForInplace();
   DcQueueList* ToSendingAndUnlock(ALocker& alocker) {
      if (!alocker.IsAllowInplace_ || this->Status_ != Status::Empty)
         return nullptr;
      this->Status_ = Status::Sending;
      alocker.UnlockForInplace();
      return &this->Sending_;
   }
   BufferList& GetQueueForPush(ALocker& alocker) {
      (void)alocker;
      assert(this->Status_ >= Status::Sending && alocker.owns_lock());
      return this->Queue_;
   }

   /// 必須自行確定: (1) 在 op safe 狀態, 或 (2) 在 op thread 裡面 且 已禁止 writable 事件.
   DcQueueList* OpImpl_ContinueSend(size_t bytesSent) {
      assert(this->Status_ == Status::Sending);
      if (bytesSent)
         this->Sending_.PopConsumed(bytesSent);
      return this->OpImpl_CheckSendQueue();
   }
   /// 必須自行確定: (1) 在 op safe 狀態, 或 (2) 在 op thread 裡面 且 已禁止 writable 事件.
   DcQueueList* OpImpl_CheckSendQueue() {
      assert(this->Status_ == Status::Sending);
      this->Sending_.push_back(std::move(this->Queue_));
      if (!this->Sending_.empty())
         return &this->Sending_;
      this->Status_ = Status::Empty;
      return nullptr;
   }
};
fon9_WARN_POP;

} } // namespaces
#endif//__fon9_io_SendBuffer_hpp__
