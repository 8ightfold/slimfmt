#define SLIMFMT_CXPR_CHECKS 0
#include <Slimfmt.hpp>
#include <iostream>

using namespace sfmt;

void testDispatch(SmallBufBase& Buf, std::string_view Str, FmtValue::List L) {
  std::printf("Buffer Size: %zu\n", Buf.capacity());
  std::printf("Format String: \"%.*s\"\n", int(Str.size()), Str.data());
  for (const FmtValue& Value : L)
    std::printf("Type: %s\n", Value.getTypeName());
}

void testDispatch(std::string_view Str, FmtValue::List L) {
  for (const FmtValue& Value : L)
    std::printf("Type: %s\n", Value.getTypeName());
}

template <std::size_t N, typename...TT>
void test(const char(&Str)[N], TT&&...Args) {
  SmallBufEstimateType<N, TT...> Buf;
  // std::printf("DEBUG: \"%s\"\n", Str);
  Formatter Fmt {Buf, Str};
  Fmt.parseWith({FmtValue{Args}...});
  Buf.pushBack('\n');
  Buf.writeTo(stdout);
  // testDispatch(Buf, Str, {FmtValue{Args}...});
}

template <std::size_t N, typename...TT>
struct testf {
  constexpr testf(FmtString<N, TT...> Str, TT&&...Args) {
    SmallBufEstimateType<N, TT...> Buf;
    testDispatch(Buf, Str, {FmtValue{Args}...});
  }
};

template <std::size_t N, typename...TT>
testf(const char(&)[N], TT&&...) -> testf<N, TT...>;

struct CustomType {
  const char* Str;
};

void format_custom(const Formatter&, const CustomType& Val) {
  std::printf("`%s`\n", Val.Str ? Val.Str : "");
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
  // std::string Str = "yeah!!";
  // CustomType  Any = {"sooo"};
  test("Testing, testing, {}!",      "123");
  test("Testing, testing, {: +10}!", "123");
  test("Testing, testing, {: =*}!",  10, "123");
  test("Testing, testing, {: -10}!", "123");
  // testf("First: {%x}, {}, {} {}!!", 42, "it's great", Any, Str);
  // testf("First: {}, {%x}, {} {}!!", "it's great", 42, Str, Any);
  // testf("First: {}, {}, {%x} {}!!", Any, "it's great", 42, Str);
  // testf("First: {}, {}, {%x} {}!!", Str, Any, "it's great", 42);
}