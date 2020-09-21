﻿// \file f9twf/TwfSymbRef.cpp
// \author fonwinz@gmail.com
#include "f9twf/TwfSymbRef.hpp"
#include "fon9/seed/FieldMaker.hpp"

namespace f9twf {

fon9::seed::Fields TwfSymbRef_MakeFields() {
   fon9::seed::Fields flds;
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriRef_,         "PriRef"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[0].Up_, "PriUpLmt"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[0].Dn_, "PriDnLmt"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.LvUpLmt_,        "LvUpLmt"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.LvDnLmt_,        "LvDnLmt"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[1].Up_, "PriUpLmt1"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[1].Dn_, "PriDnLmt1"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[2].Up_, "PriUpLmt2"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[2].Dn_, "PriDnLmt2"));

   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PPriClose_,      "PPriClose"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.PQtyTotal_,      "PQtyTotal"));
   flds.Add(fon9_MakeField(TwfSymbRef, Data_.OpenInterest_,   "OpenInterest"));

   for (unsigned L = 3; L < TwfSymbRef_Data::kPriLmtCount; ++L) {
      flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[L].Up_, fon9::RevPrintTo<std::string>("PriUpLmt", L)));
      flds.Add(fon9_MakeField(TwfSymbRef, Data_.PriLmts_[L].Dn_, fon9::RevPrintTo<std::string>("PriDnLmt", L)));
   }
   return flds;
}

} // namespaces
