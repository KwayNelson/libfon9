﻿// \file fon9/buffer/BufferList.cpp
// \author fonwinz@gmail.com
#include "fon9/buffer/BufferList.hpp"
#include "fon9/buffer/FwdBufferList.hpp"
#include "fon9/buffer/RevBufferList.hpp"
#include "fon9/buffer/DcQueueList.hpp"

namespace fon9 {

fon9_API void BufferAppendTo(const BufferNode* front, std::string& str) {
   BufferAppendTo<std::string>(front, str);
}

fon9_API void AppendToBuffer(BufferList& dst, const void* src, size_t size, size_t sizeReserved) {
   FwdBufferNode* node = FwdBufferNode::CastFrom(dst.back());
   if (node == nullptr || node->GetRemainSize() < size) {
      node = FwdBufferNode::Alloc(size + sizeReserved);
      dst.push_back(node);
   }
   byte*  ptrdst = node->GetDataEnd();
   memcpy(ptrdst, src, size);
   node->SetDataEnd(ptrdst + size);
}

RevBufferList::RevBufferList(BufferNodeSize newAllocReserved, DcQueue&& extmsg)
   : RevBuffer()
   , Builder_{newAllocReserved, dynamic_cast<DcQueueList*>(&extmsg)
                                ? static_cast<DcQueueList*>(&extmsg)->MoveOutToList()
                                : BufferList{}} {
   if (!extmsg.empty()) {
      size_t sz = extmsg.CalcSize();
      char*  pbuf = this->AllocPrefix(sz) - sz;
      this->SetPrefixUsed(pbuf);
      extmsg.Read(pbuf, sz);
   }
}

FwdBufferList::FwdBufferList(BufferNodeSize newAllocReserved, DcQueue&& extmsg)
   : FwdBuffer()
   , Builder_{newAllocReserved, dynamic_cast<DcQueueList*>(&extmsg)
                                ? static_cast<DcQueueList*>(&extmsg)->MoveOutToList()
                                : BufferList{}} {
   if (!extmsg.empty()) {
      size_t sz = extmsg.CalcSize();
      char*  pbuf = this->AllocSuffix(sz);
      this->SetSuffixUsed(pbuf + sz);
      extmsg.Read(pbuf, sz);
   }
}

} // namespaces
