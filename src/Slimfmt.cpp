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

#include "Slimfmt.hpp"
#include <cassert>
#include <cmath>
#include <charconv>
#include <ostream>
#include <tuple>

#ifdef _MSC_VER
# include <intrin.h>
#endif

#ifndef _MSC_VER
# if SLIMFMT_HAS_BUILTIN(__builtin_clzll)
#  define SLIMFMT_CLZLL(x) __builtin_clzll(x)
# endif
#else // _MSC_VER

# ifndef __clang__
#  pragma intrinsic(_BitScanForward)
#  pragma intrinsic(_BitScanReverse)
#  ifdef _WIN64
#   pragma intrinsic(_BitScanForward64)
#   pragma intrinsic(_BitScanReverse64)
#  endif // _Win64
# endif // __clang__

static inline int msc_clzll(std::uint64_t V) {
  unsigned long Out = 0;
#ifdef _WIN64
  _BitScanForward64(&Out, V);
#else // _WIN64
  if (_BitScanForward(&Out, std::uint32_t(V >> 32)))
    return 63 ^ int(Out + 32);
  _BitScanForward(&Out, std::uint32_t(V));
#endif // _WIN64
  assert(V != 0 && "Invalid clzll input!");
  return int(Out);
}

# define SLIMFMT_CLZLL(x) ::msc_clzll(x)

#endif // _MSC_VER

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

long long FmtValue::getInt(bool Permissive) const {
  using IIntType = long long;
  if SLIMFMT_UNLIKELY(!this->isIntType(Permissive))
    return 0;
  switch (this->Type) {
    case SignedType:      return Value.Signed;
    case SignedLLType:    return Value.SignedLL;
    case UnsignedType:    return IIntType(Value.Unsigned);
    case UnsignedLLType:  return IIntType(Value.UnsignedLL);
    case CharType: {
      assert(Permissive && "Error! "
        "This should never be false, "
        "please report it as a bug.");
      return IIntType(Value.Char);
    }
    default:
      SLIMFMT_UNREACHABLE;
  }
}

unsigned long long FmtValue::getUInt(bool Permissive) const {
  using IIntType = unsigned long long;
  if SLIMFMT_UNLIKELY(!this->isIntType(Permissive))
    return 0;
  switch (this->Type) {
    case UnsignedType:    return Value.Unsigned;
    case UnsignedLLType:  return Value.UnsignedLL;
    case SignedType:      return IIntType(Value.Signed);
    case SignedLLType:    return IIntType(Value.SignedLL);
    case CharType: {
      assert(Permissive && "Error! "
        "This should never be false, "
        "please report it as a bug.");
      return IIntType(Value.Char);
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
      return SLIMFMT_LIKELY(Str) ? *Str : ' ';
    }
    case StdStringType: {
      const std::string* Str = Value.StdString;
      assert(Str && "A bug occured. Please report this.");
      return SLIMFMT_LIKELY(!Str->empty())
        ? *Str->data() : ' ';
    }
    case StringViewType: {
      const std::string_view* Str = Value.StringView;
      assert(Str && "A bug occured. Please report this.");
      return SLIMFMT_LIKELY(!Str->empty())
        ? *Str->data() : ' ';
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

const void* FmtValue::getPtr(bool Permissive) const {
  if SLIMFMT_UNLIKELY(!this->isPtrType(Permissive))
    return nullptr;
  if SLIMFMT_LIKELY(this->Type == PtrType)
    return Value.Ptr;
  assert(Permissive && "Error! "
    "This should never be false, "
    "please report it as a bug.");
  return Value.CString;
}

const AnyFmt* FmtValue::getGeneric() const {
  if SLIMFMT_UNLIKELY(!this->isGenericType())
    return nullptr;
  return Value.Generic;
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

//======================================================================//
// Formatter
//======================================================================//

template <std::size_t Base>
static inline int countDigitsFallback(std::uint64_t V) {
  static constexpr std::size_t Base2 = Base * Base;
  static constexpr std::size_t Base3 = Base * Base2;
  static constexpr std::size_t Base4 = Base * Base3;
  int Total = 1;
  while (true) {
    if (V < Base)  return Total;
    if (V < Base2) return Total + 1;
    if (V < Base3) return Total + 2;
    if (V < Base4) return Total + 3;
    V /= Base4;
    Total += 4;
  }
  SLIMFMT_UNREACHABLE;
}

template <std::size_t Base = 10>
static inline int countDigits(std::uint64_t Value) {
  return countDigitsFallback<Base>(Value);
}

template <>
inline int countDigits<2>(std::uint64_t Value) {
#ifdef SLIMFMT_CLZLL
  // Mask so Value is always >1.
  return SLIMFMT_CLZLL(Value | 1);
#else
  return countDigitsFallback<2>(Value);
#endif
}

template <>
inline int countDigits<10>(std::uint64_t Value) {
#ifdef SLIMFMT_CLZLL
  // TODO: Add clzll speedup
#endif
  return countDigitsFallback<10>(Value);
}

template <std::size_t Base = 10>
static inline int countDigits(const std::int64_t Value) {
  const auto UValue = std::uint64_t(std::llabs(Value));
  return countDigits<Base>(UValue) + (Value < 0);
}

template <typename T>
static inline int countDigitsDispatch(T Value, BaseType Base) {
  switch (Base) {
   case BaseType::Bin:
    return countDigits<2>(Value);
   case BaseType::Oct:
    return countDigits<8>(Value);
   case BaseType::Dec:
    return countDigits<10>(Value);
   case BaseType::Hex:
    return countDigits<16>(Value);
   default: {
    assert(false && "Invalid base!");
    return 0;
   }
  }
}

int Formatter::CountDigits(long long Value, BaseType Base) {
  return countDigitsDispatch(std::int64_t(Value), Base);
}

int Formatter::CountDigits(unsigned long long Value, BaseType Base) {
  return countDigitsDispatch(std::uint64_t(Value), Base);
}

std::size_t Formatter::getValueSize(FmtValue Value) const {
  if SLIMFMT_UNLIKELY(Value.isGenericType()) {
    assert(false && "Cannot get the size of generic types!");
    return 0U;
  }

  // Get the current spec.
  auto& Spec = ParsedReplacement;
  if (Value.isSIntType()) {
    return CountDigits(Value.getInt(), Spec.Base);
  } else if (Value.isUIntType()) {
    return CountDigits(Value.getUInt(), Spec.Base);
  } else if (Value.isPtrType(Spec.Extra == ExtraType::Ptr)) {
    const void* Ptr = Value.getPtr(true);
    const auto IPtr = reinterpret_cast<std::uintptr_t>(Ptr);
    // Add 2 to account for the leading 0[base].
    return countDigitsDispatch(std::uint64_t(IPtr), Spec.Base) + 2;
  } else if (Value.isCharType()) {
    return 1U;
  } else if (Value.isStrType()) {
    if (Spec.Extra == ExtraType::Char)
      return 1U;
    auto [_, Len] = Value.getStr();
    return Len;
  }

  assert(false && "Invalid value type!");
  SLIMFMT_UNREACHABLE;
}

bool Formatter::formatValue(FmtValue Value) const {
  assert(ParsedReplacement.isFormat() && "Invalid formatter state!");
  // Pass off generics early, as their size cannot be determined.
  if (auto* Generic = Value.getGeneric())
    return this->write(*Generic);
  
  // Get the size of the current value for padding.
  const std::size_t Len = this->getValueSize(Value);
  auto& Spec = ParsedReplacement;

  if SLIMFMT_UNLIKELY(Spec.Base == BaseType::Invalid) {
    assert(false && "Invalid base type!");
    const auto FillAmount = std::max(Len, Spec.Align);
    Buf.fill(FillAmount, Spec.Pad);
    return false;
  }

  // Check if the alignment is actually larger than the variable length.
  // If it isn't, just write without checking the alignment type.
  if (Spec.Align < Len) {
    Buf.reserveBack(Len);
    return this->write(Value);
  }
  
  // Handle value alignment.
  Buf.reserveBack(Spec.Align);
  const std::size_t TotalAlign = Spec.Align - Len;
  if (Spec.Side == AlignType::Left) {
    bool Ret = this->write(Value);
    Buf.fill(TotalAlign, Spec.Pad);
    return Ret;
  } else if (Spec.Side == AlignType::Center) {
    const std::size_t HalfAlign = TotalAlign / 2;
    Buf.fill(HalfAlign, Spec.Pad);
    bool Ret = this->write(Value);
    Buf.fill((TotalAlign - HalfAlign), Spec.Pad);
    return Ret;
  } else /* AlignType::Right */ {
    Buf.fill(TotalAlign, Spec.Pad);
    return this->write(Value);
  }

  SLIMFMT_UNREACHABLE;
}

//=== Writers ===//

bool Formatter::write(FmtValue Value) const {
  auto& Spec = ParsedReplacement;
  /// Check if we should allow
  const bool CharPerm = (Spec.Extra == ExtraType::Char);
  const bool PtrPerm  = (Spec.Extra == ExtraType::Ptr);
  
  // We probably already handled this, but check just in case.
  if (auto* Generic = Value.getGeneric())
    return this->write(*Generic);
  else if (Value.isSIntType())
    return this->write(Value.getInt());
  else if (Value.isUIntType())
    return this->write(Value.getUInt());
  else if (Value.isPtrType(PtrPerm))
    return this->write(Value.getPtr(PtrPerm));
  else if (Value.isCharType(CharPerm))
    return this->write(Value.getChar(CharPerm));
  else if (Value.isStrType())
    return this->write(Value.getStr());
  
  assert(false && "Invalid value type!");
  SLIMFMT_UNREACHABLE;
}

// Converts value in the range [0, 100) to a string.
static constexpr const char* digitsDec2(std::size_t Value) {
  return 
    &"0001020304050607080910111213141516171819"
     "2021222324252627282930313233343536373839"
     "4041424344454647484950515253545556575859"
     "6061626364656667686970717273747576777879"
     "8081828384858687888990919293949596979899"[Value * 2];
}

// This function is the unified medium to write int-like types out.
template <std::size_t Base>
static inline bool formatUInt(SmallBufBase& Buf, std::uint64_t V, bool Upper = false) {
  static_assert(Base <= 16, "Base out of range!");
  static constexpr bool IsPow2 = false && ((Base & (Base - 1)) == 0);
  static constexpr std::size_t Base2 = (Base * Base);

  // Make a buffer that fits the digits.
  static constexpr std::size_t BufLen = (8 * 16 * 2) / Base;
  char LocalBuf[BufLen + 1] {};
  char* End = (LocalBuf + BufLen);
  char* Out = End;

  if constexpr (IsPow2) {
    do {
      const char* Digits = Upper 
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
      // Mask off the lower bits of the value.
      auto Digit = unsigned(V & ((1 << Base) - 1));
      *(--Out) = Digits[Digit];
    } while((V >>= Base) != 0U);
  } else if constexpr (Base == 10) {
    // Hardcoded this for now. Original implementation
    // can be found in fmtlib.
    while (V >= 100) {
      Out -= 2;
      const auto Str = digitsDec2(V % 100);
      Out[0] = Str[0];
      Out[1] = Str[1];
      V /= 100;
    }
    if (V < 10) {
      *(--Out) = char('0' + V);
      Buf.append(Out, End);
      return true;
    }
    Out -= 2;
    const auto Str = digitsDec2(V % 100);
    Out[0] = Str[0];
    Out[1] = Str[1];
  } else /* Not 2^N */ {
    while (V != 0) {
      const char* Digits = Upper 
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
      auto Digit = unsigned(V % Base);
      *(--Out) = Digits[Digit];
      V /= Base;
    }
  }

  Buf.append(Out, End);
  return true;
}

bool Formatter::write(unsigned long long Value) const {
  if (Value == 0) {
    Buf.pushBack('0');
    return true;
  }
  auto& Spec = ParsedReplacement;
  const bool UseUpper = 
    (Spec.Extra == ExtraType::Uppercase) ||
    (Spec.Extra == ExtraType::Ptr);
  switch (Spec.Base) {
   case BaseType::Bin:
    return formatUInt<2>(Buf, Value);
   case BaseType::Oct:
    return formatUInt<8>(Buf, Value);
   case BaseType::Dec:
    return formatUInt<10>(Buf, Value);
   case BaseType::Hex:
    return formatUInt<16>(Buf, Value, UseUpper);
   default: {
    assert(false && "Invalid base.");
    return false;
   }
  }
  return true;
}

bool Formatter::write(long long Value) const {
  if (Value < 0)
    Buf.pushBack('-');
  const unsigned long long UValue = std::llabs(Value);
  return this->write(UValue);
}

bool Formatter::write(const void* Ptr) const {
  const auto Base = ParsedReplacement.Base;
  Buf.pushBack('0');
  Buf.pushBack("bodx"[int(Base)]);
  const auto IPtr = reinterpret_cast<std::uintptr_t>(Ptr);
  return this->write((unsigned long long)IPtr);
}

bool Formatter::write(char C) const {
  Buf.pushBack(C);
  return true;
}

bool Formatter::write(FmtValue::StrAndLen FatStr) const {
  const auto [Str, Len] = FatStr;
  if SLIMFMT_UNLIKELY(!Str)
    return false;
  // TODO: Check for ExtraType::Char here?
  Buf.append(Str, Str + Len);
  return true;
}

bool Formatter::write(const AnyFmt& Generic) const {
  Generic.doFormat(*this);
  return true;
}

//=== Spec Parsing ===//

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

static std::pair<BaseType, ExtraType> parseRSpecOptions(StrView S) {
  if (S.empty())
    return {BaseType::Default, ExtraType::Default};
  
  // Valid spec options can currently only be 2 characters long.
  assert(S.size() < 3 && "Spec options too long!");
  BaseType Base = BaseType::Default;
  ExtraType Extra = ExtraType::Default;

  // Recurse to find the back, then continue.
  if (S.size() == 2) {
    // We start from the back to support things like %op.
    // Starting from the front would require more checks.
    std::tie(Base, Extra) = 
      parseRSpecOptions({&S.back(), 1});
    S.remove_suffix(1);
  }

  // Determine the Option type.
  switch (S[0]) {
   // Bases:
   case 'B':
   case 'b': {
    Base = BaseType::Bin;
    break;
   }
   case 'O':
   case 'o': {
    Base = BaseType::Oct;
    break;
   }
   case 'D':
   case 'd': {
    Base = BaseType::Dec;
    break;
   }
   case 'H':
   case 'X':
    Extra = ExtraType::Uppercase;
   case 'x':
   case 'h': {
    Base = BaseType::Hex;
    break;
   }

   // Extra:
   case 'P':
   case 'p': {
    assert(Extra == ExtraType::None && 
      "Type specifier will be overwritten!");
    Base = BaseType::Hex;
    // Pointers always print lowercase.
    Extra = ExtraType::Ptr;
    break;
   }
   case 'C':
   case 'c': {
    assert(Extra == ExtraType::None && 
      "Type specifier will be overwritten!");
    Extra = ExtraType::Char;
    break;
   }
   default:
    assert(false && "Invalid spec option!");
  }

  return {Base, Extra};
}

bool Formatter::parseReplacementSpec(StrView Spec) {
  char Pad = ' ';
  AlignType Side = AlignType::Default;
  std::size_t Align = 0;
  BaseType  Base  = BaseType::Default;
  ExtraType Extra = ExtraType::Default;
  StrView Data = Spec;
  /// Use this to exit early without duplication.
  auto Finish = [&, this]() -> bool {
    this->ParsedReplacement = 
      FmtReplacement(Data, Base, Extra, Side, Align, Pad);
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
    if SLIMFMT_UNLIKELY(Pad < ' ' || Pad > 0x7F) {
      assert(false && "Invalid padding type!");
      Pad = ' ';
    }
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

  std::tie(Base, Extra) = parseRSpecOptions(Spec.substr(1));
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

//=== Core ===//

namespace {
  struct FmtValueSpan {
  #if !defined(__clang__) && (__GNUC__ >= 9)
  # pragma GCC diagnostic push
  # pragma GCC diagnostic ignored "-Winit-list-lifetime"
  #endif
    explicit FmtValueSpan(FmtValue::List Values) :
     Begin(Values.begin()), End(Values.end()) {}
  #if !defined(__clang__) && (__GNUC__ >= 9)
  # pragma GCC diagnostic pop
  #endif
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
    if (!this->formatValue(*Value))
      // An error occurred. Stop parsing.
      return;
  }

  assert(Vs.isEmpty() && "Too many arguments passed to formatter!");
}
