//===- Slimfmt.cpp --------------------------------------------------===//
//
// Copyright (C) 2024 Eightfold
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
//     limitations under the License.
//
//===----------------------------------------------------------------===//

#define SLIMFMT_CXPR_CHECKS 0
#include "Slimfmt.hpp"
#include <cassert>
#include <charconv>
#include <ostream>

using namespace sfmt;
using namespace sfmt::H;

namespace {
  template <typename T>
  struct MMatch {
    using RawType = H::RemoveCVRef<T>;
  public:
    constexpr MMatch(T Val) : Data(Val) {}

    template <typename U>
    constexpr bool is(U&& Arg) const {
      return Data == static_cast<RawType>(Arg);
    }
    template <typename...TT>
    constexpr bool is(TT&&...Args) const {
      return (false || ... || is(static_cast<TT>(Args)));
    }

  private:
    T Data;
  };

  template <typename T>
  MMatch(T&) -> MMatch<T&>;

  template <typename T>
  MMatch(T&&) -> MMatch<std::remove_const_t<T>>;
} // namespace `anonymous`

//======================================================================//
// DynBuf
//======================================================================//

char* DynBuf::setBufferAndCapacity(char* Ptr, size_type Cap) {
  char* OldPtr = this->Data;
  this->Data = Ptr;
  this->Capacity = Cap;
  this->tweakBuffer();
  return OldPtr;
}

void DynBuf::tweakBuffer() {
  this->tweakCapacity();
  if (this->Data == nullptr)
    this->Data = AllocateBuffer(this->Capacity);
}

void DynBuf::tweakCapacity() {
  if (this->Data)
    return;
  if (Capacity == 0)
    this->Capacity = 1;
}

//======================================================================//
// SmallBufImpl
//======================================================================//

SmallBufImpl::SmallBufImpl(SmallBufImpl&& Other) :
 DynBuf(static_cast<DynBuf&&>(Other)) {
  // Immediately clear if the buffer isn't inlined.
  if (!Other.isSelfUsingInlinedBuffer()) {
    Other.clear();
    return;
  }
  char* NewPtr = this->AllocateBuffer(this->Capacity);
  std::memcpy(NewPtr, this->Data, this->size());
  this->setBufferAndCapacity(NewPtr, this->Capacity);
  Other.resetSize();
}

void SmallBufImpl::writeTo(std::basic_ostream<char>& OS) {
  if (const char* Ptr = this->data(); SLIMFMT_LIKELY(Ptr))
    OS.write(Ptr, this->size());
}

void SmallBufImpl::writeTo(std::FILE* File) {
  if (const char* Ptr = this->data(); SLIMFMT_LIKELY(Ptr))
    std::fwrite(Ptr, 1, this->size(), File);
}

void SmallBufImpl::append(const char* Begin, const char* End) {
  const size_type Total = (End - Begin);
  if (Total == 0)
    return;
  this->tryReserve(this->Size + Total);
  // We will append from the end pointer.
  // This is done after reserving in case a reallocation occurs.
  std::memcpy(this->end(), Begin, Total);
  // Set the new size.
  this->Size += Total;
}

void SmallBufImpl::tryResize(size_type Count, char Fill) {
  const size_type OldSize = this->size();
  if SLIMFMT_UNLIKELY(!tryResizeForFill(Count))
    return;
  H::assume(this->size() > OldSize);
  const size_type Len = (this->size() - OldSize);
  std::memset(this->getNthElem(OldSize), int(Fill), Len);
}

bool SmallBufImpl::tryResizeForFill(size_type Count) {
  const bool DoFill = (Count > this->size());
  this->tryResize(Count);
  return DoFill;
}

void SmallBufImpl::tryReserve(size_type Cap) {
  const size_type OldCapacity = this->Capacity;
  if SLIMFMT_LIKELY(Cap <= OldCapacity)
    return;
  size_type NewCapacity = OldCapacity + (OldCapacity / 2);
  NewCapacity = std::max(NewCapacity, Cap);
  assert(NewCapacity < DynBuf::MaxSize() && "Range error!");
  char* OldPtr = this->Data;
  char* NewPtr = this->AllocateBuffer(NewCapacity);
  // Suppress overflow warnings.
  H::assume(this->size() <= NewCapacity);
  this->setBufferAndCapacity(NewPtr, NewCapacity);
  if SLIMFMT_LIKELY(OldPtr != nullptr) {
    std::memcpy(NewPtr, OldPtr, this->size());
    // Free if OldPtr isn't the inlined pointer.
    if (!isInlinedBuffer(OldPtr))
      delete[] OldPtr;
  }
}

void SmallBufImpl::move(SmallBufImpl& Other) {
  if SLIMFMT_UNLIKELY(&Other == this)
    return;
  if SLIMFMT_UNLIKELY(Other.isEmpty()) {
    Other.resetSize();
    this->resetSize();
    return;
  }
  if (this->capacity() < Other.capacity()) {
    this->cloneOrTakeBuffer(Other);
    return;
  }
  // At this point we know the other buffer's capacity
  // is less than ours, and it isn't empty. This means
  // we can just reset the size and copy the data.
  this->resetSize();
  this->append(Other.begin(), Other.end());
  Other.deallocateIfDynamic();
}

void SmallBufImpl::cloneOrTakeBuffer(SmallBufImpl& Other) {
  this->deallocateIfDynamic();
  if SLIMFMT_UNLIKELY(Other.isSelfUsingInlinedBuffer()) {
    this->tryReserve(Other.capacity());
    this->append(Other.begin(), Other.end());
    Other.resetSize();
    return;
  }
  // Take the other buffer and clear.
  this->setBufferAndCapacity(
    Other.data(), Other.capacity());
  Other.clearUnsafe();
}

void SmallBufImpl::deallocateIfDynamic() {
  deallocateIfDynamicFast();
  clearOrReset();
}

void SmallBufImpl::clearOrReset() {
  if SLIMFMT_LIKELY(!this->isSelfUsingInlinedBuffer())
    // If the buffer is dynamic, we want to completely clear.
    this->clear();
  else
    // Otherwise, just reset the size so the buffer doesn't
    // "forget" that it's using an inlined array.
    this->resetSize();
}

//======================================================================//
// FmtValue
//======================================================================//

bool FmtValue::isSIntType(bool Permissive) const noexcept {
  const bool Extra = Permissive && (Type == CharType);
  return Extra || MMatch(Type).is(
    SignedType, SignedLLType);
}

bool FmtValue::isUIntType(bool Permissive) const noexcept {
  const bool Extra = Permissive && (Type == CharType);
  return Extra || MMatch(Type).is(
    UnsignedType, UnsignedLLType);
}

bool FmtValue::isIntType(bool Permissive) const noexcept {
  const bool Extra = Permissive && (Type == CharType);
  return Extra || MMatch(Type).is(
    SignedType, UnsignedType, 
    SignedLLType, UnsignedLLType);
}

bool FmtValue::isStrType(bool Permissive) const noexcept {
  const bool Extra = Permissive && (Type == CharType);
  return Extra || MMatch(Type).is(
    CStringType, StdStringType, 
    StringViewType);
}

bool FmtValue::isCharType(bool Permissive) const noexcept {
  if SLIMFMT_LIKELY(!Permissive)
    return (Type == CharType);
  return this->isStrType(false);
}

unsigned long long FmtValue::getInt(bool Permissive) const {
  if SLIMFMT_UNLIKELY(!this->isIntType(Permissive))
    return 0;
  switch (this->Type) {
    case SignedType:      return Value.Signed;
    case UnsignedType:    return Value.Unsigned;
    case SignedLLType:    return Value.SignedLL;
    case UnsignedLLType:  return Value.UnsignedLL;
    case CharType: {
      assert(Permissive && "Error! "
        "This should never be false, "
        "please report it as a bug.");
      return unsigned(Value.Char);
    }
    default:
      SLIMFMT_UNREACHABLE;
  }
}

char FmtValue::getChar(bool Permissive) const {
  if SLIMFMT_UNLIKELY(!this->isCharType(Permissive))
    return '\0';
  if (Type == CharType)
    return Value.Char;
  assert(Permissive && "Error! "
    "This should never be false, "
    "please report it as a bug.");
  switch (this->Type) {
    case CStringType: {
      const char* Str = Value.CString;
      return SLIMFMT_LIKELY(Str) ? *Str : '\0';
    }
    case StdStringType: {
      const std::string* Str = Value.StdString;
      assert(Str && "A bug occured. Please report this.");
      return SLIMFMT_LIKELY(!Str->empty())
        ? *Str->data() : '\0';
    }
    case StringViewType: {
      const std::string_view* Str = Value.StringView;
      assert(Str && "A bug occured. Please report this.");
      return SLIMFMT_LIKELY(!Str->empty())
        ? *Str->data() : '\0';
    }
    default:
      SLIMFMT_UNREACHABLE;
  }
}

FmtValue::StrAndLen FmtValue::getStr(bool Permissive) const {
  if SLIMFMT_UNLIKELY(!this->isStrType(Permissive))
    return {nullptr, 0};
  switch (this->Type) {
    case CStringType: {
      const char* Str = Value.CString;
      const std::size_t Len = Str ? std::strlen(Str) : 0;
      return {Str, Len};
    }
    case StdStringType: {
      const std::string* Str = Value.StdString;
      assert(Str && "A bug occured. Please report this.");
      return {Str->c_str(), Str->size()};
    }
    case StringViewType: {
      const std::string_view* Str = Value.StringView;
      assert(Str && "A bug occured. Please report this.");
      return {Str->data(), Str->size()};
    }
    case CharType: {
      assert(Permissive && "Error! "
        "This should never be false, "
        "please report it as a bug.");
      return {&Value.Char, 1U};
    }
    default:
      SLIMFMT_UNREACHABLE;
  }
}

const char* FmtValue::getTypeName() const {
  switch (this->Type) {
   case CharType:       return "Char";
   case SignedType:     return "Signed";
   case SignedLLType:   return "SignedLL";
   case UnsignedType:   return "Unsigned";
   case UnsignedLLType: return "UnsignedLL";
   case PtrType:        return "Ptr";
   case CStringType:    return "CString";
   case StdStringType:  return "StdString";
   case StringViewType: return "StringView";
   default:             return "Generic";
  }
}

//=== Actual Formatting ===//

namespace {
  enum : bool {
    Valid   = false,
    Invalid = true,
  };
} // namespace `anonymous`

static bool formatToStr(
 const Formatter& Fmt, const char* Str, std::size_t Len) {
  auto& Spec = Fmt.getLastReplacement();
  if (Len >= Spec.Align) {
    Fmt->append(Str, Str + Len);
    return Valid;
  }

  Fmt->reserveBack(Spec.Align);
  const std::size_t FillTotal = (Spec.Align - Len);

  if SLIMFMT_LIKELY(Spec.Side == AlignType::Left) {
    Fmt->append(Str, Str + Len);
    Fmt->fill(FillTotal, Spec.Pad);
    return Valid;
  } else if (Spec.Side == AlignType::Center) {
    const std::size_t FillHalf = FillTotal / 2;
    Fmt->fill(FillHalf, Spec.Pad);
    Fmt->append(Str, Str + Len);
    Fmt->fill(FillHalf, Spec.Pad);
    // Check if total is odd. If it is, append
    // a padding character.
    if (FillTotal % 2)
      Fmt->pushBack(Spec.Pad);
    return Valid;
  } else {
    Fmt->fill(FillTotal, Spec.Pad);
    Fmt->append(Str, Str + Len);
    return Valid;
  }

  return Invalid;
}

bool FmtValue::formatTo(const Formatter& Fmt) const {
  const bool Perm = Fmt.isPermissive();
  assert(Fmt.getLastReplacement().isFormat() && "Formatter in invalid state!");

  // String has priority over Char. This means 
  // the latter will never get hit in permissive mode.
  if (this->isStrType(Perm)) {
    auto [Str, Len] = this->getStr(Perm);
    return formatToStr(Fmt, Str, Len);
  } else if (this->isCharType()) {
    char C = this->getChar();
  }

  return Invalid;
}

//======================================================================//
// Formatter
//======================================================================//

namespace {
  struct FmtValueSpan {
    explicit FmtValueSpan(FmtValue::List Values) :
     Begin(Values.begin()), End(Values.end()) {}
  public:
    const FmtValue* take() {
      if SLIMFMT_UNLIKELY(this->isEmpty())
        return nullptr;
      return this->Begin++;
    }
    std::pair<const FmtValue*, const FmtValue*> takePair() {
      const auto First = this->take();
      return {First, this->take()};
    }
    bool canTake() const {
      return !this->isEmpty();
    }
    bool canTakePair() const {
      return this->size() > 1;
    }
    std::size_t size() const {
      return End - Begin;
    }
    bool isEmpty() const {
      return Begin == End;
    }
  
  private:
    const FmtValue* Begin;
    const FmtValue* End;
  };
} // namespace `anonymous`

void Formatter::setReplacementSubstr(std::size_t Len) {
  if (Len == StrView::npos)
    Len = FormatString.size();
  this->ParsedReplacement = 
    FmtReplacement(FormatString.substr(0, Len));
}

void Formatter::setReplacementSubstr(std::size_t Pos, std::size_t Len) {
  this->ParsedReplacement = 
    FmtReplacement(FormatString.substr(Pos, Len));
}

StrView Formatter::collectBraces() const {
  const auto BraceEnd = FormatString.find_first_not_of('{');
  if SLIMFMT_UNLIKELY(BraceEnd == StrView::npos)
    return StrView();
  return FormatString.substr(0, BraceEnd);
}

//=== Parsing ===//

static AlignType parseRSpecSide(StrView& Spec) {
  if SLIMFMT_UNLIKELY(Spec.empty())
    return AlignType::Default;
  const char AlignChar = Spec.front();
  Spec.remove_prefix(1);
  switch (AlignChar) {
    // Left
    case '+':
    case '<': return AlignType::Left;
    // Center
    case '=':
    case ' ': return AlignType::Center;
    // Right
    case '-':
    case '>': return AlignType::Right;
    default: {
      assert(false && "Invalid alignment specifier!");
      return AlignType::Default;
    }
  }
  SLIMFMT_UNREACHABLE;
}

static std::size_t parseRSpecAlign(StrView& Spec) {
  if SLIMFMT_UNLIKELY(Spec.empty())
    return 0;
  if (Spec.front() == '*') {
    Spec.remove_prefix(1);
    return FmtReplacement::dynamicAlign;
  }

  // Look for the next sections spec identifier.
  // If it doesn't exist, just use the whole string.
  std::size_t DigitCount = Spec.find_first_of('%');
  DigitCount = std::min(DigitCount, Spec.size());

  const char* End = Spec.data() + DigitCount;
  std::size_t Output;
  auto [Ptr, EC] = std::from_chars(Spec.data(), End, Output);
  Spec.remove_prefix(DigitCount);

  // Check the error code.
  if SLIMFMT_UNLIKELY(EC != std::errc{}) {
    auto Err = std::make_error_code(EC).message();
    std::fprintf(stderr, "\"%s\" at %c.\n", Err.c_str(), *Ptr);
    assert(false && "Invalid width specifier!");
    return 0;
  }

  return Output;
}

bool Formatter::parseReplacementSpec(StrView Spec) {
  char Pad = ' ';
  AlignType Side = AlignType::Default;
  std::size_t Align = 0;
  StrView Options = "";
  StrView Data = Spec;
  /// Use this to exit early without duplication.
  auto Finish = [&, this]() -> bool {
    this->ParsedReplacement = 
      FmtReplacement(Data, Options, Side, Align, Pad);
    return true;
  };

  if (Spec.empty())
    return Finish();
  /// Check if we have an align/width specifier.
  if (Spec.front() == ':') {
    if SLIMFMT_UNLIKELY(Spec.size() <= 1) {
      assert(false && "Spec string not long enough!");
      this->ParsedReplacement = FmtReplacement();
      return false;
    }
    Pad = Spec[1];
    assert(Pad >= ' ' && "Invalid padding type!");
    Spec.remove_prefix(2);
    Side  = parseRSpecSide(Spec);
    Align = parseRSpecAlign(Spec);
    if (Spec.empty())
      return Finish();
  }
  // The only possible format specifier now is %.
  // If it doesn't currently start with this, the
  // format specifier is invalid.
  if SLIMFMT_UNLIKELY(Spec.front() != '%' || Spec.size() <= 1) {
    assert(false && "Invalid extra format specifier!");
    this->ParsedReplacement = FmtReplacement();
    return false;
  }

  Options = Spec.substr(1);
  return Finish();
}

bool Formatter::parseNextReplacement() {
  if (FormatString.empty())
    return false;
  
  if (FormatString.front() != '{') {
    std::size_t BraceOpen = FormatString.find_first_of('{');
    BraceOpen = std::min(BraceOpen, FormatString.size());
    this->setReplacementSubstr(BraceOpen);
    FormatString.remove_prefix(BraceOpen);
    return true;
  }

  StrView Braces = this->collectBraces();
  // If we hit this, some of the braces are escaped.
  // Treat these as replacements.
  if (Braces.size() > 1) {
    const std::size_t NReplacements = Braces.size() / 2;
    this->setReplacementSubstr(NReplacements);
    FormatString.remove_prefix(NReplacements * 2);
    return true;
  }

  std::size_t BraceClose = FormatString.find_first_of('}');
  if (BraceClose == StrView::npos) {
    assert(false && "Unterminated format specifier. "
      "Use {{ to escape a sequence.");
    this->setReplacementSubstr();
    FormatString = "";
    return false;
  }

  const std::size_t NextBraceOpen =
    FormatString.find_first_of('{', 1);
  // If we hit this, there is another sequence after
  // the current brace. Treat this section as a literal
  // and continue.
  if (NextBraceOpen < BraceClose) {
    this->setReplacementSubstr(NextBraceOpen);
    FormatString.remove_prefix(NextBraceOpen);
    return true;
  }

  StrView Spec = FormatString.substr(1, BraceClose - 1);
  FormatString.remove_prefix(BraceClose + 1);
  return parseReplacementSpec(Spec);
}

void Formatter::parseWith(FmtValue::List Values) {
  FmtValueSpan Vs {Values};
  while (this->parseNextReplacement()) {
    if SLIMFMT_UNLIKELY(ParsedReplacement.isEmpty()) {
      assert(false && "Parse Failure!");
      return;
    }
    // Check if format is normal string.
    if (ParsedReplacement.isLiteral()) {
      Buf.appendStr(ParsedReplacement.Data);
      continue;
    }
    // If the format specifier used dynamic alignment (*),
    // we extract an argument as an integer, and use that as the value.
    if (ParsedReplacement.hasDynAlign()) {
      assert(Vs.canTakePair() && "Not enough arguments for dynamic align!");
      const FmtValue* Align = Vs.take();
      assert(Align->isIntType(true) && "Invalid dynamic alignment type!");
      ParsedReplacement.Align = Align->getInt(true);
    }
    // Use the value as the dispatcher for parsing.
    // There should be at least one argument here.
    if SLIMFMT_UNLIKELY(!Vs.canTake()) {
      assert(false && "Not enough arguments!");
      return;
    }
    const FmtValue* Value = Vs.take();
    if (Value->formatTo(*this))
      // An error occurred. Stop parsing.
      return;
  }

  assert(Vs.isEmpty() && "Too many arguments passed to formatter!");
}
