#define SLIMFMT_CXPR_CHECKS 0
#include <Slimfmt.hpp>
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

int main() {
  namespace chrono = std::chrono;
  using TimerType = chrono::high_resolution_clock;

  std::string Str = "yeah!!";
  CustomType  Any = {"sooo"};

  const std::int64_t Iters = 100000;
  auto Start = TimerType::now();
  for (std::int64_t I = 0; I < Iters; ++I) {
    sfmt::nulls("Testing, testing, {}!",      "123");
    sfmt::nulls("Testing, testing, {: +10}!", 123);
    sfmt::nulls("Testing, testing, {: =*%D}!", 10, "123");
    sfmt::nulls("Testing, testing, {: -10}!", 123);
    sfmt::nulls("Testing, testing, {%c}!", "ABC");
    sfmt::nulls("{%b}, {}, {} {}!!", 42, "it's great", Any, Str);
    sfmt::nulls("{}, {%o}, {} {}!!", "it's great", 42, Str, Any);
    sfmt::nulls("{}, {}, {%d} {}!!", Any, "it's great", 42, Str);
    sfmt::nulls("{}, {}, {} {%X}!!", Str, Any, "it's great", 42);
    sfmt::nulls("\n\n");
    sfmt::nulls("Testing, testing, {}!!",      "123");
    sfmt::nulls("Testing, testing, {: +10}!!", 123);
    sfmt::nulls("Testing, testing, {: =*%D}!!", 10, "123");
    sfmt::nulls("Testing, testing, {: -10}!!", 123);
    sfmt::nulls("Testing, testing, {%c}!!", "ABC");
    sfmt::nulls("{%b}, {}, {} {}!", 42, "it's great", Any, Str);
    sfmt::nulls("{}, {%o}, {} {}!", "it's great", 42, Str, Any);
    sfmt::nulls("{}, {}, {%d} {}!", Any, "it's great", 42, Str);
    sfmt::nulls("{}, {}, {} {%X}!", Str, Any, "it's great", 42);
  }
  auto End = TimerType::now();
  const chrono::duration<double> Secs = End - Start;

  std::cout << "Took " << Secs.count() << "s to do "
    << Iters << " iterations." << std::endl;
}