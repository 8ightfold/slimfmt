//===- Slimfmt.hpp --------------------------------------------------===//
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

#pragma once

#ifndef SLIMFMT_HSLIMFMT_HPP
#define SLIMFMT_HSLIMFMT_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef SLIMFMT_FORCE_ASSERT
# define SLIMFMT_FORCE_ASSERT 0
#endif

#ifndef SLIMFMT_STDERR_ASSERT
# define SLIMFMT_STDERR_ASSERT 0
#endif

#ifdef __has_cpp_attribute
# define SLIMFMT_HAS_CPP_ATTR(x) (__has_cpp_attribute(x))
#else
# define SLIMFMT_HAS_CPP_ATTR(x) (0)
#endif // __has_cpp_attribute?

#ifdef __has_builtin
# define SLIMFMT_HAS_BUILTIN(x) (__has_builtin(x))
#else
# define SLIMFMT_HAS_BUILTIN(x) (0)
#endif // __has_builtin?

#ifndef SLIMFMT_CONSTEVAL
# undef SLIMFMT_HAS_CONSTEVAL
# if __cpp_consteval >= 201811L
#  define SLIMFMT_CONSTEVAL consteval
#  define SLIMFMT_HAS_CONSTEVAL 1
# else
#  define SLIMFMT_CONSTEVAL constexpr
#  define SLIMFMT_HAS_CONSTEVAL 0
# endif
#endif // SLIMFMT_CONSTEVAL

#if SLIMFMT_HAS_CPP_ATTR(no_unique_address)
# if defined(_MSC_VER) && !defined(__GNUC__)
#  define SLIMFMT_NO_UNIQUE_ADDR [[msvc::no_unique_address]]
# else // Not MSVC
#  define SLIMFMT_NO_UNIQUE_ADDR [[no_unique_address]]
#endif
#else
# define SLIMFMT_NO_UNIQUE_ADDR
#endif

#if SLIMFMT_HAS_BUILTIN(__builtin_expect)
# define SLIMFMT_LIKELY(ex)   (__builtin_expect(bool(ex), true))
# define SLIMFMT_UNLIKELY(ex) (__builtin_expect(bool(ex), false))
#else
# define SLIMFMT_LIKELY(ex)   (bool(ex))
# define SLIMFMT_UNLIKELY(ex) (bool(ex))
#endif

#define SLIMFMT_ASSUME(...) ::sfmt::H::assume(bool(__VA_ARGS__))

#ifdef NDEBUG
# define SLIMFMT_UNREACHABLE ::sfmt::H::unreachable()
#else
# define SLIMFMT_UNREACHABLE do { \
  assert(false && "Reached an unreachable position."); \
  ::sfmt::H::unreachable(); \
} while(0)
#endif

//======================================================================//
// Utilities
//======================================================================//

// Use `*::H` as the detail namespace to keep symbols small.
namespace sfmt::H {

#if __cpp_lib_remove_cvref >= 201711L
/// Alias for `std::remove_cvref_t`.
template <typename T>
using RemoveCVRef = std::remove_cvref_t<T>;
#else
/// Implementation of `std::remove_cvref_t` for C++17.
template <typename T>
using RemoveCVRef = std::remove_cv_t<std::remove_reference_t<T>>;
#endif

/// Workaround for `(void)arg` not working on some compilers.
template <typename...TT>
inline constexpr void ignore_args(TT&...) {}

/// Checks if currently constant evaluating.
inline constexpr bool
 is_consteval([[maybe_unused]] bool V = false) {
#ifdef __cpp_lib_is_constant_evaluated
  return std::is_constant_evaluated();
#elif SLIMFMT_HAS_BUILTIN(__builtin_is_constant_evaluated)
  return __builtin_is_constant_evaluated();
#elif defined(_GLIBCXX_RELEASE) && (_GLIBCXX_RELEASE >= 12)
  return __builtin_is_constant_evaluated();
#else
  return V;
#endif
}

/// Hints to the compiler that a condition is always true.
inline void assume([[maybe_unused]] bool Cond) {
#if SLIMFMT_HAS_CPP_ATTR(assume)
  [[assume(Cond)]];
#elif __cpp_lib_unreachable >= 202202L
  if (!Cond) std::unreachable();
#elif defined(_MSC_VER) && !defined(__GNUC__)
  __assume(Cond);
#elif !defined(__clang__)
  // clang hinders optimizations with complex assumptions.
# if SLIMFMT_HAS_BUILTIN(__builtin_assume)
  __builtin_assume(Cond);
# elif SLIMFMT_HAS_BUILTIN(__builtin_unreachable)
  if (!Cond) __builtin_unreachable();
# endif
#elif !defined(NDEBUG)
  assert(Cond && "Assumption was false!");
#endif
}

[[noreturn]] inline void unreachable() {
#if __cpp_lib_unreachable >= 202202L
  std::unreachable();
#elif SLIMFMT_HAS_BUILTIN(__builtin_unreachable)
  __builtin_unreachable();
#elif defined(_MSC_VER) && !defined(__GNUC__)
  __assume(false);
#endif
}

} // namespace sfmt::H

namespace sfmt {
/// Alias for `std::string_view`.
using StrView = std::string_view;
} // namespace sfmt

//======================================================================//
// Buffering
//======================================================================//

namespace sfmt::H {

/// A dynamically allocated buffer for strings.
/// Do not use this directly as it does not automatically free.
struct DynBuf {
  using value_type = char;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;

  using iterator = value_type*;
  using const_iterator = const value_type*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

public:
  DynBuf(char* Ptr, size_type Capacity) : 
   Data(Ptr), Capacity(Capacity) {
    // Always have some capacity.
    this->tweakBuffer();
  }

  DynBuf(const DynBuf&) = delete;
  DynBuf& operator=(const DynBuf&) = delete;

  DynBuf(DynBuf&& Other) :
   Data(Other.Data), 
   Size(Other.Size), 
   Capacity(Other.Capacity) {
    Other.Size = 0;
  }

public:
  size_type size() const { return this->Size; }
  size_type capacity() const { return this->Capacity; }

  static constexpr size_type MaxSize() {
    static_assert(std::is_unsigned_v<size_type>);
    return ~size_type(0) - 1;
  }

  bool isEmpty() const { return size() == 0; }
  bool isFull()  const { return size() == capacity(); }

  iterator       begin()       { return this->Data; }
  const_iterator begin() const { return this->Data; }
  iterator       end()         { return begin() + size(); }
  const_iterator end()   const { return begin() + size(); }

  reverse_iterator       rbegin()       { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const { return const_reverse_iterator(begin()); }
  reverse_iterator       rend()         { return reverse_iterator(end()); }
  const_reverse_iterator rend()   const { return const_reverse_iterator(end()); }

  pointer data() { return begin(); }
  const_pointer data() const { return begin(); }

  reference operator[](size_type Idx) {
    assert(Idx < size());
    return Data[Idx];
  }
  const_reference operator[](size_type Idx) const {
    assert(Idx < size());
    return Data[Idx];
  }

  reference front() {
    assert(!isEmpty());
    return begin()[0];
  }
  const_reference front() const {
    assert(!isEmpty());
    return begin()[0];
  }

  reference back() {
    assert(!isEmpty());
    return end()[-1];
  }
  const_reference back() const {
    assert(!isEmpty());
    return end()[-1];
  }

protected:
  static char* AllocateBuffer(size_type Cap) {
    return new char[Cap];
  }

  /// Sets the new buffer pointer and cap.
  /// @return The old buffer pointer.
  char* setBufferAndCapacity(char* Ptr, size_type Cap);

  /// Sets the capacity to 1 if none, then allocates a new buffer.
  void tweakBuffer();
  void tweakCapacity();

  void deallocate() {
    delete[] this->Data;
  }

  void resetSize() {
    this->Size = 0;
  }

  void clear() {
    resetSize();
    this->Data = nullptr;
    this->Capacity = 0;
  }

protected:
  char* Data = nullptr;
  size_type Size = 0, Capacity;
};

struct SmallBufAlignAndSize {
  alignas(DynBuf) char Base[sizeof(DynBuf)];
  char FirstElem[1];
};

class SmallBufImpl : public DynBuf {
  using DynBuf::deallocate;
  using DynBuf::tweakCapacity;
  using DynBuf::clear;
  using DynBuf::Data;
  using DynBuf::Size;
  using DynBuf::Capacity;
public:
  using DynBuf::size_type;
protected:
  SmallBufImpl(size_type Cap) :
   DynBuf(Cap ? getFirstElem() : nullptr, Cap) {}
  
  SmallBufImpl(SmallBufImpl&& Other);

public:
  void writeTo(std::basic_ostream<char>& OS);
  void writeTo(std::FILE* File);

  void pushBack(char Val) {
    this->tryReserve(this->Size + 1);
    this->Data[this->Size++] = Val;
  }

  void append(const char* Begin, const char* End);

  template <typename It>
  void append(const It* Begin, const It* End) {
    const size_type Total = (End - Begin);
    if (Total == 0)
      return;
    this->tryReserve(this->Size + Total);
    // Save the end position. We will append from here.
    // This is done after reserving in case a reallocation occurs.
    char* OffPtr = this->end();
    if constexpr (std::is_same_v<char, It>) {
      std::memcpy(OffPtr, Begin, Total);
    } else {
      for (size_type Ix = 0; Ix < Total; ++Ix)
        // Massage and assign elements from the input range.
        OffPtr[Ix] = static_cast<char>(Begin[Ix]);
    }
    // Set the new size.
    this->Size += Total;
  }

  template <typename It>
  void append(const It* Begin, std::size_t Len) {
    if SLIMFMT_UNLIKELY(!Begin || Len == 0)
      return;
    this->append(Begin, Begin + Len);
  }

  void appendStr(StrView Str) {
    this->append(Str.begin(), Str.end());
  }

  template <typename T>
  void appendRange(const T& Range) {
    this->append(Range.data(), Range.data() + Range.size());
  }

  void fill(size_type Count, char Fill) {
    this->tryResize(this->size() + Count, Fill);
  }
  void resizeBack(size_type Count) {
    this->tryResize(this->size() + Count);
  }
  void reserveBack(size_type Count) {
    this->tryReserve(this->size() + Count);
  }

  void tryResize(size_type Count) {
    this->tryReserve(Count);
    this->Size = std::min(Count, this->Capacity);
  }

  void tryResize(size_type Count, char Fill);
  bool tryResizeForFill(size_type Count);
  void tryReserve(size_type Cap);

protected:
  /// Moves another buffer into this one.
  void move(SmallBufImpl& Other);

  /// Used when the internal capacity is known to be less than
  /// that of `Other`, and the buffer isn't empty.
  void cloneOrTakeBuffer(SmallBufImpl& Other);

  /// Deallocates and clears buffer. This is the function that should
  /// be used in 99% of cases, as it automatically handles clearing.
  void deallocateIfDynamic();

  /// Deallocates without clearing. 
  /// Only used during destruction.
  void deallocateIfDynamicFast() {
    if SLIMFMT_LIKELY(!this->isSelfUsingInlinedBuffer())
      DynBuf::deallocate();
  }

  /// Clears without deallocating.
  void clearOrReset();

  /// Clears without checking if inlined.
  void clearUnsafe() {
    assert(!this->isSelfUsingInlinedBuffer());
    this->clear();
  }

  bool isSelfUsingInlinedBuffer() const {
    return isInlinedBuffer(this->Data);
  }
  
  bool isInlinedBuffer(const char* Ptr) const {
    return Ptr == this->getFirstElem(); 
  }

  char* getFirstElem() {
    const SmallBufImpl* const self = this;
    return const_cast<char*>(self->getFirstElem());
  }
  const char* getFirstElem() const {
    const size_type Off = offsetof(SmallBufAlignAndSize, FirstElem);
    return reinterpret_cast<const char*>(this) + Off;
  }

  char* getNthElem(size_type At) {
    assert(At < this->size());
    return &this->Data[At];
  }
};

/// The underlying storage for `SmallBuf`.
template <std::size_t Size>
struct SmallBufStorage {
protected:
  char* getBufPtr() { return this->Storage; }
  const char* getBufPtr() const { return this->Storage; }

  bool inBufRange(const char* Ptr) const {
    const auto BufPtr = getBufPtr();
    return Ptr >= BufPtr && Ptr < (BufPtr + Size);
  }

protected:
  char Storage[Size];
};

/// Specialization for zero sized buffers.
template <>
struct alignas(char) SmallBufStorage<0U> {
protected:
  char* getBufPtr() { return nullptr; }
  const char* getBufPtr() const { return nullptr; }
  bool inBufRange(const char*) const { return false; }
};

} // namespace sfmt::H

namespace sfmt {

/// An alias for `SmallBufImpl`.
using SmallBufBase = H::SmallBufImpl;

/// A buffer which can avoid dynamic allocation for a certain amount of elements.
/// @tparam InlinedSize The size of the inlined element buffer.
template <std::size_t InlinedSize>
struct SmallBuf : public H::SmallBufImpl, H::SmallBufStorage<InlinedSize> {
  using SelfType = SmallBuf<InlinedSize>;
  using BaseType = H::SmallBufImpl;
  using StoType  = H::SmallBufStorage<InlinedSize>;
  using BaseType::value_type;
  using BaseType::size_type;
public:
  explicit constexpr SmallBuf() :
   H::SmallBufImpl(InlinedSize) {
    assert(this->data() && "Buffer cannot be null!");
    if SLIMFMT_UNLIKELY(H::is_consteval())
      std::fill_n(this->begin(), this->capacity(), '\0');
  }

  SmallBuf(BaseType&& Other) :
   H::SmallBufImpl(InlinedSize) {
    this->move(Other);
  }

  SmallBuf& operator=(BaseType&& Other) {
    assert(!isReferenceToSelf(Other) && "Moved into self!");
    this->move(Other);
    return *this;
  }

  ~SmallBuf() { this->deallocateIfDynamicFast(); }

public:
  void resize(size_type Count) {
    this->tryResize(Count);
  }

  void resize(size_type Count, char Fill) {
    this->tryResize(Count, Fill);
  }

  void reserve(size_type Cap) {
    this->tryReserve(Cap); 
  }

  [[maybe_unused]] inline void wipe() {
    this->deallocateAndSetInlineBuf();
  }

private:
  void deallocateAndSetInlineBuf() {
    this->deallocateIfDynamic();
    this->setBufferAndCapacity(
      this->getBufPtr(), InlinedSize);
  }

  bool isReferenceToSelf(const BaseType& Other) const {
    return &Other == static_cast<const BaseType*>(this);
  }
};

} // namespace sfmt

//======================================================================//
// Any
//======================================================================//

namespace sfmt {

struct Formatter;

/// An implementation of any which doesn't use dynamic allocation.
class Any {
  template <typename T>
  /// Not `const` to avoid MSVC merging all the definitions.
  /// Initialized to 1 to force ID to be placed in the .data section.
  struct Type { inline static char ID = 1; };

public:
  Any() : Data(nullptr), ID(nullptr) {}
  template <typename T> Any(T&&) = delete;

  template <typename T> Any(const T& Value) : 
   Data(&Value), ID(&Type<T>::ID) {}
  
  template <typename T> Any(T& Value) :
   Data(&Value), ID(&Type<T>::ID) {}

  Any& operator=(Any&& Other) {
    this->Data = Other.Data;
    this->ID   = Other.ID;
    Other.reset();
    return *this;
  }

public:
  /// Resets the `Any` to an empty state.
  void reset() {
    this->Data = nullptr;
    this->ID   = nullptr;
  }

  /// Returns `true` if the `Any` has no value.
  bool isEmpty() const {
    return (Data == nullptr) || (ID == nullptr);
  }

  template <typename T>
  bool isA() const {
    if SLIMFMT_UNLIKELY(this->isEmpty())
      return false;
    return (ID == &Type<T>::ID);
  }

  template <typename T>
  auto getAs() -> const H::RemoveCVRef<T>* {
    using Type = H::RemoveCVRef<T>;
    if SLIMFMT_UNLIKELY(!isA<T>())
      return nullptr;
    return static_cast<const Type*>(this->Data);
  }

public:
  const void* Data;
  const void* ID;
};

struct AnyFmt : public Any {
  using FmtFnType = void(*)(const Formatter&, Any&);
public:
  AnyFmt() : Any(), Format(GetFormatter()) {}

  template <typename T> AnyFmt(T& Value) :
   Any(Value), Format(GetFormatter(Value)) {}
  
  template <typename T> AnyFmt(const T& Value) :
   Any(Value), Format(GetFormatter(Value)) {}

public:
  /// Runs the format function with the current object.
  void doFormat(const Formatter& Fmt) const {
    this->Format(Fmt, const_cast<AnyFmt&>(*this));
  }

private:
  template <typename T>
  static void RunFormatter(
   const Formatter& Fmt, Any& Value) {
    if (const T* Ptr = Value.getAs<T>())
      format_custom(Fmt, *Ptr);
  }

  static void EmptyFormatter(const Formatter&, Any&) {}

  template <typename T>
  static FmtFnType GetFormatter(const T&) {
    return &AnyFmt::RunFormatter<T>;
  }

  static FmtFnType GetFormatter() {
    return &AnyFmt::EmptyFormatter;
  }

public:
  FmtFnType Format;
};

} // namespace sfmt

//======================================================================//
// Formatting
//======================================================================//

namespace sfmt {

class FmtValue {
  friend struct Formatter;

  enum ValueType : std::uint8_t {
    CharType,
    SignedType,
    UnsignedType,
    SignedLLType,
    UnsignedLLType,
    PtrType,
    CStringType,
    StdStringType,
    StringViewType,
    GenericType
  };

  union Wrapper {
    char Char;
    int Signed;
    unsigned Unsigned;
    long long SignedLL;
    unsigned long long UnsignedLL;
    const void* Ptr;
    const char* CString;
    const std::string* StdString;
    const StrView* StringView;
    const AnyFmt* Generic;
  };

public:
  using StrAndLen = std::pair<const char*, std::size_t>;
  using List = std::initializer_list<FmtValue>;

  /// Checks if the current value is a signed integral.
  /// @param Permissive If `char` is considered an int.
  bool isSIntType(bool Permissive = false) const noexcept;

  /// Checks if the current value is a signed integral.
  /// @param Permissive If `char` is considered an int.
  bool isUIntType(bool Permissive = false) const noexcept;

  /// Checks if the current value is an integral.
  /// @param Permissive If `char` is considered an int.
  bool isIntType(bool Permissive = false) const noexcept;

  /// Checks if the current value is a character.
  /// @param Permissive If string types are considered a "characters".
  bool isCharType(bool Permissive = false) const noexcept;

  /// Checks if the current value is a string type.
  /// @param Permissive If `char` is considered a "string".
  bool isStrType(bool Permissive = false) const noexcept;

  /// Checks if the current value is a pointer type.
  /// @param Permissive If Cstrings are considered a pointer.
  bool isPtrType(bool Permissive = false) const noexcept {
    const bool Extra = Permissive && (Type == CStringType);
    return Extra || (Type == PtrType);
  }

  /// Checks if the current value is a user-defined type.
  bool isGenericType() const noexcept {
    return Type == GenericType;
  }

  /// Extracts the current value as an integer.
  /// @returns `0` if an error occurred (check `isIntType()`).
  long long getInt(bool Permissive = false) const;

  /// Extracts the current value as an unsigned integer.
  /// @returns `0` if an error occurred (check `isIntType()`).
  unsigned long long getUInt(bool Permissive = false) const;

  /// Extracts the current value as a character.
  /// @returns `'\0'` if an error occurred.
  char getChar(bool Permissive = false) const;

  /// Extracts the current value as a `{Ptr, Len}` pair.
  /// @returns `{nullptr, 0}` if an error occurred.
  StrAndLen getStr(bool Permissive = false) const;

  /// Extracts the current value as a constant opaque pointer.
  /// @returns `nullptr` if an error occured (check `isPtrType()`).
  const void* getPtr(bool Permissive = false) const;

  /// Extracts the current value as a generic.
  /// @returns `nullptr` if an error occurred.
  const AnyFmt* getGeneric() const;

  /// Gets the name of the active value type.
  /// @returns A constant string.
  const char* getTypeName() const;

public:
  FmtValue(char C) : Type(CharType) {
    Value.Char = C;
  }

  FmtValue(signed char V) : Type(SignedType) {
    Value.Signed = V;
  }

  FmtValue(unsigned char V) : Type(UnsignedType) {
    Value.Unsigned = V;
  }

  FmtValue(int V) : Type(SignedType) {
    Value.Signed = V;
  }

  FmtValue(unsigned V) : Type(UnsignedType) {
    Value.Unsigned = V;
  }

  FmtValue(long long V) : Type(SignedLLType) {
    Value.SignedLL = V;
  }

  FmtValue(unsigned long long V) : Type(UnsignedLLType) {
    Value.UnsignedLL = V;
  }

  FmtValue(std::nullptr_t) : Type(PtrType) {
    Value.Ptr = nullptr;
  }

  FmtValue(const void* Ptr) : Type(PtrType) {
    Value.Ptr = Ptr;
  }

  FmtValue(const char* Str) : Type(CStringType) {
    Value.CString = Str;
  }

  FmtValue(const std::string& Str) : Type(StdStringType) {
    Value.StdString = &Str;
  }

  FmtValue(const StrView& Str) : Type(StringViewType) {
    Value.StringView = &Str;
  }

  FmtValue(const AnyFmt& Generic) : Type(GenericType) {
    Value.Generic = &Generic;
  }

private:
  Wrapper Value;
  ValueType Type;
};

//=== Format Specific ===//

using RawBaseType = std::int64_t;
enum class BaseType : RawBaseType {
  Bin = 2, 
  Oct = 8, 
  Dec = 10,
  Hex = 16,
  Default = Dec,
  Invalid = -1
};

enum class ExtraType {
  None, Uppercase, Char, Ptr,
  Default = None
};

enum class AlignType {
  Left, Right, Center,
  Default = Left
};

struct BaseSink {
  constexpr BaseSink() = default;
  constexpr BaseSink(std::int64_t Base) : RawValue(Base) {}
  constexpr BaseSink(BaseType Base) : BaseSink(RawBaseType(Base)) {}
  
  BaseSink& operator=(RawBaseType Base) {
    this->RawValue = Base;
    return *this;
  }
  BaseSink& operator=(BaseType Base) {
    this->RawValue = RawBaseType(Base);
    return *this;
  }

  constexpr operator RawBaseType() const {
    return this->RawValue;
  }
  explicit constexpr operator BaseType() const {
    return BaseType(this->RawValue);
  }

  constexpr friend bool operator==(
   const BaseSink& Lhs, const BaseSink& Rhs) {
    return Lhs.RawValue == Rhs.RawValue;
  }

public:
  RawBaseType RawValue = 10;
};

struct FmtReplacement {
  enum RType { Empty, Literal, Format };
  static constexpr std::size_t dynamicAlign = ~std::size_t(0);
public:
  FmtReplacement() = default;
  explicit FmtReplacement(StrView Str) :
   Type(Literal), Data(Str) {}
  
  FmtReplacement(
    StrView Spec, BaseSink Base, ExtraType Extra,
    AlignType Side, std::size_t Align, char Pad = ' ') :
   Type(Format), Data(Spec), Base(Base), Extra(Extra),
   Side(Side), Align(Align), Pad(Pad) {}

public:
  bool isEmpty()   const { return Type == RType::Empty; }
  bool isLiteral() const { return Type == RType::Literal; }
  bool isFormat()  const { return Type == RType::Format; }
  bool hasDynAlign() const { return Align == dynamicAlign; }

public:
  RType Type = RType::Empty;
  StrView Data;
  BaseSink Base = BaseType::Default;
  ExtraType Extra = ExtraType::Default;
  std::size_t Align = 0;
  AlignType Side = AlignType::Default;
  char Pad = '\0';
};

struct Formatter {
  Formatter(SmallBufBase& Buf, StrView Str, 
    bool Permissive = false) : 
   FormatString(Str), Buf(Buf), IsPermissive(Permissive) {}
public:
  SmallBufBase* operator->() const { return &Buf; }
  bool isPermissive() const { return this->IsPermissive; }
  const FmtReplacement& getLastReplacement() const& {
    return this->ParsedReplacement;
  }

  bool parseNextReplacement();
  bool parseReplacementSpec(StrView Spec);
  void parseWith(FmtValue::List Values);

public:
  static int CountDigits(long long Value, BaseSink Base);
  static int CountDigits(unsigned long long Value, BaseSink Base);
  std::size_t getValueSize(FmtValue Value) const;

  /// Formats an abstract value with the current parse state.
  bool formatValue(FmtValue Value) const;

  bool write(FmtValue Value) const;
  bool write(unsigned long long Value) const;
  bool write(long long Value) const;
  bool write(const void* Ptr) const;
  bool write(char C) const;
  bool write(FmtValue::StrAndLen FatStr) const;
  bool write(const AnyFmt& Generic) const;
  bool write(const SmallBufBase& InBuf) const;

protected:
  void setReplacementSubstr(std::size_t Len = StrView::npos);
  void setReplacementSubstr(std::size_t Pos, std::size_t Len);
  StrView collectBraces() const;

private:
  StrView FormatString;
  FmtReplacement ParsedReplacement;
  SmallBufBase& Buf;
  const bool IsPermissive;
};

} // namespace sfmt

namespace sfmt {

template <std::size_t N>
using SmallBufEstimateType = 
  SmallBuf<(N > 64) ? 256 : 128>;

template <std::size_t N, typename...TT>
void printerCommon(StrView Str, SmallBuf<N>& Buf, const TT&...Args) {
  Formatter Fmt {Buf, Str};
  Fmt.parseWith({FmtValue{Args}...});
}

template <std::size_t N, typename...TT>
void printerlnCommon(StrView Str, SmallBuf<N>& Buf, const TT&...Args) {
  printerCommon(Str, Buf, Args...);
  Buf.pushBack('\n');
}

//======================================================================//
// API
//======================================================================//

template <std::size_t N, typename...TT>
std::string format(const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N> Buf;
  printerCommon({Str, N}, Buf, Args...);
  return std::string(Buf.begin(), Buf.end());
}

template <std::size_t N, typename...TT>
void nulls(const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N> Buf;
  printerCommon({Str, N}, Buf, Args...);
}

template <std::size_t N, typename...TT>
void print(std::FILE* File, const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N> Buf;
  printerCommon({Str, N}, Buf, Args...);
  Buf.writeTo(File);
}

template <std::size_t N, typename...TT>
void print(std::ostream& Stream, const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N> Buf;
  printerCommon({Str, N}, Buf, Args...);
  Buf.writeTo(Stream);
}

template <std::size_t N, typename...TT>
void print(const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N> Buf;
  printerCommon({Str, N}, Buf, Args...);
  Buf.writeTo(stdout);
}

template <std::size_t N, typename...TT>
void println(std::FILE* File, const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N + 1> Buf;
  printerlnCommon({Str, N}, Buf, Args...);
  Buf.writeTo(File);
}

template <std::size_t N, typename...TT>
void println(std::ostream& Stream, const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N + 1> Buf;
  printerlnCommon({Str, N}, Buf, Args...);
  Buf.writeTo(Stream);
}

template <std::size_t N, typename...TT>
void println(const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N> Buf;
  printerlnCommon({Str, N}, Buf, Args...);
  Buf.writeTo(stdout);
}

/// @brief Enables or disables colored output.
/// @return The old color mode value.
bool setColorMode(bool Value);

} // namespace sfmt

#endif // SLIMFMT_HSLIMFMT_HPP
