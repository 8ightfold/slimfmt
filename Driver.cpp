#define SLIMFMT_CXPR_CHECKS 0
#include <Slimfmt.hpp>
#include <cmath>
#include <chrono>
#include <iostream>

using namespace sfmt;

struct CustomType {
  const char* Str;
};

void format_custom(const Formatter& Fmt, const CustomType& Val) {
  const std::size_t Len = (Val.Str ? std::strlen(Val.Str) : 0);
  Fmt.write({Val.Str, Len});
}

void testBuf() {
  SmallBuf<16> Buf {};
  Buf.pushBack('H');
  Buf.appendStr("ello world!");
  // Reallocates
  Buf.appendStr(" Yeah let's add a reaaaallly long string here...\n");
  Buf.writeTo(std::cout);

  SmallBuf<4> OtherBuf {};
  OtherBuf.appendStr("Rahhh!");
  Buf = std::move(OtherBuf);
  OtherBuf.wipe();
  Buf.writeTo(stdout);

  OtherBuf.pushBack(' ');
  OtherBuf.resize(4, '.');
  OtherBuf.writeTo(std::cout);

  OtherBuf.resize(0);
  OtherBuf.appendStr("ok.\n");
  OtherBuf.writeTo(stdout);
}

template <typename F>
static inline void runOneTest(std::string& Str, CustomType& Any, F&& Func) {
  Func("Testing, testing, {}!",      "123");
  Func("Testing, testing, {:#9}!",  "123");
  Func("Testing, testing, {:# 9}!", "123");
  Func("Testing, testing, {: +9}!", 123);
  Func("Testing, testing, {: =*%D}!", 9, "123");
  Func("Testing, testing, {: -9}!", 123);
  Func("Testing, testing, {%c}!", "ABC");
  Func("{%b}, {}, {} {}!!", 42, "it's great", Any, Str);
  Func("{}, {%o}, {} {}!!", "it's great", 42, Str, Any);
  Func("{}, {}, {%d} {}!!", Any, "it's great", 42, Str);
  Func("{}, {}, {} {%X}!!", Str, Any, "it's great", 42);
  Func("\n\n");
  Func("Testing, testing, {}!!",      "123");
  Func("Testing, testing, {: +10%x}!!", -123);
  Func("Testing, testing, {: =*}!!", 10, "-7b");
  Func("Testing, testing, {: -10%x}!!", -123);
  Func("Testing, testing, {%c}!!", "ABC");
  Func("{%b}, {}, {} {}!", 42, "it's great", Any, Str);
  Func("{}, {%o}, {} {}!", "it's great", 42, Str, Any);
  Func("{}, {}, {%d} {}!", Any, "it's great", 42, Str);
  Func("{}, {}, {} {%X}!", Str, Any, "it's great", 42);
}

static inline void runOneTest(std::string& Str, CustomType& Any) {
  runOneTest(Str, Any, [](auto&&...Args) {
    sfmt::println(Args...);
  });
}

sfmt::Printer& getDbgErrorPrinter(bool IsDebug) {
  if (IsDebug)
    return sfmt::errln;
  else
    return sfmt::null;
}

void dbgTest(bool IsDebug) {
  auto& Dbg = getDbgErrorPrinter(IsDebug);
  Dbg("{}, {}, {}\n", 'x', 'y', 'z');
}

struct Test {
  operator std::string() const {
    return "Yello";
  }
  operator std::string_view() const {
    return "Yello";
  }
  operator unsigned() const {
    return sizeof("Yello") - 1;
  }
};

void format_custom(const Formatter& Fmt, const Test& T) {
  Fmt->reserveBack(5 + 2);
  Fmt->pushBack('"');
  Fmt->appendStr(T);
  Fmt->pushBack('"');
}

template <std::size_t N, typename...TT>
void printTypes(const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<sizeof...(TT) * 8> Buf {};
  FmtValue Data[] { SLIMFMT_ARG(Args)... };
  constexpr std::size_t ArgCount = sizeof...(TT);
  for (std::size_t I = 0; I < ArgCount; ++I) {
    Buf.appendStr(Data[I].getTypeName());
    if SLIMFMT_LIKELY((I + 1) < ArgCount)
      Buf.appendStr(", ");
  }
  Buf.appendStr(":\n");
  Buf.writeTo(stdout);
  sfmt::println(Str, Args...);
}

void testTypes() {
  std::string S = "Zello";
  std::string& SR = S;
  const std::string& SCR = SR;

  StrView SV = "Mello";
  StrView& SVR = SV;
  const StrView& SVCR = SVR;

  Test T {};
  Test& TR = T;
  const Test& TCR = TR;

  printTypes(
    "({}, {}, {}), ({}, {}, {}), ({}, {}, {})",
    S, SR, SCR, SV, SVR, SVCR, T, TR, TCR);
}

int main() {
  namespace chrono = std::chrono;
  using TimerType = chrono::high_resolution_clock;
  sfmt::setColorMode(true);

  std::string Str = "yeah!!";
  CustomType  Any = {"sooo"};

  sfmt::null("static constexpr std::uint64_t baseLog2LUT[] {{\n  ");
  sfmt::null("0, ");
  for (int I = 1; I <= 32; ++I) {
    const auto Log = std::log2(double(I));
    sfmt::null("{}, ", std::uint64_t(Log));
    if ((I % 8) == 0)
      sfmt::null("\n  ");
  }
  sfmt::null("};\n");

  sfmt::outln("{%r32}, {%r25}, {%r8}, {%r5}\n", 789942, 59922, 98311, 588585);
  sfmt::println("{%r32p}!!\n", "Yello");

  constexpr std::int64_t Iters = 100000;
  auto Start = TimerType::now();
  for (std::int64_t I = 0; I < Iters; ++I) {
    runOneTest(Str, Any, [](auto&&...Args) {
      (void) sfmt::test(Args...);
    });
  }
  auto End = TimerType::now();
  const chrono::duration<double> Secs = End - Start;

  std::cout << "Took " << Secs.count() << "s to do "
    << Iters << " iterations." << std::endl;
  
  dbgTest(true);
  dbgTest(false);

  sfmt::null("{}", Test{});
  testTypes();
}
