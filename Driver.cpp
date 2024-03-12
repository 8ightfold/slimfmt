#define SLIMFMT_CXPR_CHECKS 0
#include <Slimfmt.hpp>
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
  std::string Str = "yeah!!";
  CustomType  Any = {"sooo"};
  sfmt::println("Testing, testing, {}!",      "123");
  sfmt::println("Testing, testing, {: +10}!", 123);
  sfmt::println("Testing, testing, {: =*}!",  10, "123");
  sfmt::println("Testing, testing, {: -10}!", 123);
  sfmt::println("{%b}, {}, {} {}!!", 42, "it's great", Any, Str);
  sfmt::println("{}, {%o}, {} {}!!", "it's great", 42, Str, Any);
  sfmt::println("{}, {}, {%d} {}!!", Any, "it's great", 42, Str);
  sfmt::println("{}, {}, {} {%X}!!", Str, Any, "it's great", 42);
}