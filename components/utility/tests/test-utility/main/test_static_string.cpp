// Tests for the fixed-capacity NUL-terminated string
// (thermo::utility::static_string).
//
// static_string wraps thermo::utility::static_vector<char, N + 1> and
// guarantees a valid NUL terminator, so c_str() is always safe. `N` counts
// characters only (the terminator has its own reserved slot), so `remaining()`
// is exactly how many characters can still be appended and the try_* appends
// succeed while the whole addition fits. It is pure
// logic with no IDF dependencies and is covered on the host. Overflow asserts
// (a programming error) rather than truncating, so the boundary cases below
// exercise exact-fit operations only. The asserting overflow paths are verified
// in forked children that exec the standalone `static_string_assert` program
// (see the [assert] cases at the end).
#include <compare>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>

#include "assert_unity_support.hpp"
#include "unity.h"
#include "utility/static_string.hpp"

using thermo::utility::static_string;

static_assert(std::is_constructible_v<static_string<8>, std::string_view>);
static_assert(!std::is_convertible_v<std::string_view, static_string<8>>);

// --- constexpr usability ----------------------------------------------------
//
// Evaluates the string at compile time, so it fails to build if any exercised
// operation stops being constexpr.
namespace {

constexpr bool static_string_constexpr_builds() {
    static_string<8> s("hello");
    const bool built = s.size() == 5 && !s.empty() && s.capacity() == 8 &&
                       s.remaining() == 3 && s.c_str()[0] == 'h' &&
                       s[4] == 'o' && *s.begin() == 'h' && *s.cend() == '\0';

    s.push_back('!');
    const bool pushed =
        s.size() == 6 && s.c_str()[5] == '!' && s.remaining() == 2;

    // Fill to capacity, then confirm the try_* appends refuse and the string
    // is still intact.
    s.assign("abcdefgh");
    const bool filled = s.remaining() == 0 && !s.try_push_back('!') &&
                        !s.try_append("x", 1) && s.c_str()[8] == '\0';

    // A null C string is still a constant expression: the null check happens
    // before the length walk.
    s.assign(static_cast<const char*>(nullptr));
    const bool null_assign = s.empty() && s.c_str()[0] == '\0';
    s.assign("abc");
    s.append(static_cast<const char*>(nullptr));
    const bool null_append = s.size() == 3 && s.c_str()[3] == '\0';
    s = static_string<8>(static_cast<const char*>(nullptr));
    const bool null_construct = s.empty();

    // A string view keeps its full bounded length, including embedded NULs.
    const static_string<8> from_view(std::string_view("view"));
    constexpr char view_raw[] = {'a', '\0', 'b'};
    const static_string<8> from_view_with_nul(
        std::string_view(view_raw, sizeof(view_raw)));
    const bool string_view_constructed =
        from_view.size() == 4 && from_view.c_str()[4] == '\0' &&
        from_view_with_nul.size() == 3 && from_view_with_nul[1] == '\0';

    // std::move is deliberate here: it must select the rvalue overload, even
    // though static_string is trivially copyable and clang-tidy treats the move
    // as a no-op. Reading a moved-from source is the behavior under test.
    // NOLINTBEGIN(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    // Cross-capacity operations stay usable in constant expressions.
    static_string<3> source("abc");
    const static_string<8> copied(source);
    static_string<8> copy_assigned;
    copy_assigned = source;
    const static_string<8> moved(std::move(source));
    static_string<3> move_source("abc");
    static_string<8> move_assigned;
    move_assigned = std::move(move_source);
    const bool cross_capacity =
        copied.size() == 3 && copy_assigned.size() == 3 && moved.size() == 3 &&
        source.empty() && move_assigned.size() == 3 && move_source.empty();

    // The explicit same-capacity special members stay constexpr too.
    static_string<8> same_source("xyz");
    const static_string<8> same_copied(same_source);
    static_string<8> same_copy_assigned;
    same_copy_assigned = same_source;
    static_string<8> same_move_source("xyz");
    const static_string<8> same_moved(std::move(same_move_source));
    static_string<8> same_move_source_2("xyz");
    static_string<8> same_move_assigned;
    same_move_assigned = std::move(same_move_source_2);
    const bool same_capacity =
        same_copied.size() == 3 && same_copy_assigned.size() == 3 &&
        same_moved.size() == 3 && same_move_assigned.size() == 3 &&
        same_source.size() == 3 && same_move_source.size() == 3 &&
        same_move_source_2.size() == 3;
    // NOLINTEND(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)

    s.assign("abc");
    const bool assigned = s.size() == 3 && s.c_str()[0] == 'a';

    s.clear();
    const bool cleared = s.empty() && s.c_str()[0] == '\0';

    s.assign("abc");
    const bool back = s.back() == 'c';
    s.back() = 'C';
    const bool back_mutated = s.size() == 3 && s[2] == 'C';
    s.pop_back();
    const bool popped = s.size() == 2 && s.back() == 'b' && s[2] == '\0';
    s.pop_back();
    s.pop_back();
    const bool emptied_by_pop = s.empty() && s.c_str()[0] == '\0';

    return built && pushed && filled && null_assign && null_append &&
           null_construct && string_view_constructed && cross_capacity &&
           same_capacity && assigned && cleared && back && back_mutated &&
           popped && emptied_by_pop;
}

}  // namespace

static_assert(static_string_constexpr_builds(),
              "static_string must be usable in constant expressions");

// --- constexpr comparisons --------------------------------------------------
//
// The comparison operators go through `std::strncmp`, which is a builtin on
// this toolchain, so they must stay usable in constant expressions too.
namespace {

constexpr bool static_string_constexpr_compares() {
    constexpr static_string<8> abc("abc");
    constexpr static_string<3> abc_tight("abc");

    // Equal contents across capacities, both against each other and against a
    // C string.
    const bool equal_across_capacities = abc == abc_tight && abc_tight == abc;
    const bool equal_to_c_str = abc == "abc" && abc_tight == "abc";
    const bool unequal = abc != "abd" && abc != static_string<8>("ab");

    // Ordering: a prefix sorts first, then plain character order.
    const bool ordered = abc < "abd" && static_string<8>("ab") < abc &&
                         abc > "abb" && abc_tight >= abc && abc_tight <= abc;

    // Three-way comparison agrees with the relational operators.
    const bool spaceship = (abc <=> abc_tight) == std::strong_ordering::equal &&
                           (abc <=> "abd") == std::strong_ordering::less &&
                           (abc <=> "abb") == std::strong_ordering::greater;

    return equal_across_capacities && equal_to_c_str && unequal && ordered &&
           spaceship;
}

}  // namespace

static_assert(static_string_constexpr_compares(),
              "static_string comparisons must be usable in constant "
              "expressions");

// --- Construction and capacity ----------------------------------------------

TEST_CASE("static_string: default is empty and NUL-terminated",
          "[static_string]") {
    static_string<8> s;
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_TRUE(s.empty());
    TEST_ASSERT_EQUAL_UINT(8, s.capacity());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
    TEST_ASSERT_NOT_NULL(s.data());
}

TEST_CASE("static_string: construct from a C string", "[static_string]") {
    static_string<8> s("hello");
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_FALSE(s.empty());
    TEST_ASSERT_EQUAL_STRING("hello", s.c_str());
}

TEST_CASE("static_string: a null C string yields an empty string",
          "[static_string]") {
    static_string<8> s(static_cast<const char*>(nullptr));
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
}

TEST_CASE("static_string: construct from a string_view", "[static_string]") {
    constexpr std::string_view view("hello");
    const static_string<8> s(view);
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_FALSE(s.empty());
    TEST_ASSERT_EQUAL_STRING("hello", s.c_str());
}

TEST_CASE("static_string: an empty string_view yields an empty string",
          "[static_string]") {
    const static_string<8> s(std::string_view{});
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
}

TEST_CASE("static_string: construct from an exactly-capacity string_view",
          "[static_string]") {
    constexpr std::string_view view("12345678");
    const static_string<8> s(view);
    TEST_ASSERT_EQUAL_UINT(8, s.size());
    TEST_ASSERT_EQUAL_STRING("12345678", s.c_str());
}

TEST_CASE("static_string: string_view construction preserves embedded NULs",
          "[static_string]") {
    constexpr char raw[] = {'a', '\0', 'b'};
    const static_string<8> s(std::string_view(raw, sizeof(raw)));
    TEST_ASSERT_EQUAL_UINT(3, s.size());
    TEST_ASSERT_EQUAL_CHAR('a', s[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', s[1]);
    TEST_ASSERT_EQUAL_CHAR('b', s[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', s.c_str()[3]);
}

TEST_CASE("static_string: copy-construct across capacities when the value fits",
          "[static_string]") {
    const static_string<3> source("abc");
    const static_string<8> destination(source);
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_STRING("abc", source.c_str());
}

TEST_CASE("static_string: move-construct across capacities empties the source",
          "[static_string]") {
    static_string<3> source("abc");
    // The rvalue overload is selected deliberately; inspecting the source
    // afterward verifies that this overload empties it.
    // NOLINTBEGIN(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    const static_string<8> destination(std::move(source));
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_TRUE(source.empty());
    // NOLINTEND(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

TEST_CASE("static_string: copy-assign across capacities when the value fits",
          "[static_string]") {
    static_string<8> destination("original");
    const static_string<3> source("abc");
    destination = source;
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_STRING("abc", source.c_str());
}

TEST_CASE("static_string: move-assign across capacities empties the source",
          "[static_string]") {
    static_string<8> destination("original");
    static_string<3> source("abc");
    // The rvalue overload is selected deliberately; inspecting the source
    // afterward verifies that this overload empties it.
    // NOLINTBEGIN(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    destination = std::move(source);
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_TRUE(source.empty());
    // NOLINTEND(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

TEST_CASE("static_string: cross-capacity copy preserves embedded NULs",
          "[static_string]") {
    static_string<5> source;
    constexpr char raw[] = {'a', '\0', 'b'};
    source.append(raw, sizeof(raw));

    static_string<8> destination(source);
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_CHAR('a', destination[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', destination[1]);
    TEST_ASSERT_EQUAL_CHAR('b', destination[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', destination.c_str()[3]);
}

TEST_CASE("static_string: copy-construct same capacity preserves both values",
          "[static_string]") {
    const static_string<8> source("abc");
    const static_string<8> destination(source);
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_STRING("abc", source.c_str());
}

TEST_CASE("static_string: move-construct same capacity copies the value",
          "[static_string]") {
    static_string<8> source("abc");
    // The rvalue overload is selected deliberately; the defaulted move is a
    // byte copy, so the source is unchanged and static_string stays trivially
    // copyable for SequenceLock.
    // NOLINTBEGIN(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    const static_string<8> destination(std::move(source));
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_STRING("abc", source.c_str());
    // NOLINTEND(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

TEST_CASE("static_string: copy-assign same capacity preserves both values",
          "[static_string]") {
    static_string<8> destination("original");
    const static_string<8> source("abc");
    destination = source;
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_STRING("abc", source.c_str());
}

TEST_CASE("static_string: copy-assign to self keeps the value",
          "[static_string]") {
    static_string<8> s("abc");
    static_string<8>& alias = s;
    s = alias;
    TEST_ASSERT_EQUAL_STRING("abc", s.c_str());
    TEST_ASSERT_EQUAL_UINT(3, s.size());
}

TEST_CASE("static_string: move-assign same capacity copies the value",
          "[static_string]") {
    static_string<8> destination("original");
    static_string<8> source("abc");
    // The rvalue overload is selected deliberately; the defaulted move is a
    // byte copy, so the source is unchanged and static_string stays trivially
    // copyable for SequenceLock.
    // NOLINTBEGIN(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    destination = std::move(source);
    TEST_ASSERT_EQUAL_STRING("abc", destination.c_str());
    TEST_ASSERT_EQUAL_UINT(3, destination.size());
    TEST_ASSERT_EQUAL_STRING("abc", source.c_str());
    // NOLINTEND(performance-move-const-arg,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

TEST_CASE("static_string: move-assign to self keeps the value",
          "[static_string]") {
    static_string<8> s("abc");
    static_string<8>& alias = s;
    // NOLINTNEXTLINE(performance-move-const-arg)
    s = std::move(alias);
    TEST_ASSERT_EQUAL_STRING("abc", s.c_str());
    TEST_ASSERT_EQUAL_UINT(3, s.size());
}

// --- c_str always NUL-terminated --------------------------------------------

TEST_CASE("static_string: c_str is NUL-terminated at size", "[static_string]") {
    static_string<8> s("abc");
    TEST_ASSERT_EQUAL_CHAR('\0', s.c_str()[3]);
    TEST_ASSERT_EQUAL_CHAR('\0', s.data()[s.size()]);
    TEST_ASSERT_EQUAL_UINT(3, std::strlen(s.c_str()));
}

// --- Exact-fit boundary -----------------------------------------------------

TEST_CASE("static_string: exactly-capacity construction fits",
          "[static_string]") {
    // N == 5 stores 5 characters in the buffer's 6 slots (5 + the NUL).
    static_string<5> s("abcde");
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
    TEST_ASSERT_EQUAL_CHAR('\0', s.c_str()[5]);
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

// --- append / assign --------------------------------------------------------

TEST_CASE("static_string: operator+= appends text and characters",
          "[static_string]") {
    static_string<8> s("ab");
    s += "cd";
    s += 'e';
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
    TEST_ASSERT_EQUAL_UINT(5, s.size());
}

TEST_CASE("static_string: append concatenates", "[static_string]") {
    static_string<10> s("foo");
    s.append("bar");
    TEST_ASSERT_EQUAL_UINT(6, s.size());
    TEST_ASSERT_EQUAL_STRING("foobar", s.c_str());
}

TEST_CASE("static_string: append fills exactly to capacity",
          "[static_string]") {
    static_string<5> s("abc");
    s.append("de");  // Fits exactly: remaining() == 2.
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

TEST_CASE("static_string: append a null pointer is a no-op",
          "[static_string]") {
    static_string<8> s("keep");
    s.append(nullptr);
    TEST_ASSERT_EQUAL_STRING("keep", s.c_str());
}

TEST_CASE("static_string: assign replaces the contents", "[static_string]") {
    static_string<8> s("original");
    s.assign("new");
    TEST_ASSERT_EQUAL_UINT(3, s.size());
    TEST_ASSERT_EQUAL_STRING("new", s.c_str());
}

TEST_CASE("static_string: assign accepts an exactly-capacity string",
          "[static_string]") {
    static_string<7> s("x");
    s.assign("toolong");  // 7 characters, exactly capacity().
    TEST_ASSERT_EQUAL_UINT(7, s.size());
    TEST_ASSERT_EQUAL_STRING("toolong", s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

// --- push_back / clear ------------------------------------------------------

TEST_CASE("static_string: push_back appends single characters",
          "[static_string]") {
    static_string<4> s;
    s.push_back('h');
    s.push_back('i');
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("hi", s.c_str());
}

TEST_CASE("static_string: push_back fills exactly to capacity",
          "[static_string]") {
    static_string<2> s;
    s.push_back('a');
    s.push_back('b');  // Fills the last free slot.
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("ab", s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

TEST_CASE("static_string: clear leaves an empty terminated string",
          "[static_string]") {
    static_string<8> s("data");
    s.clear();
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
}

// --- back / pop_back --------------------------------------------------------

TEST_CASE("static_string: back reads and mutates the last character",
          "[static_string]") {
    static_string<8> s("abc");
    const auto& constant = s;

    TEST_ASSERT_EQUAL_CHAR('c', s.back());
    TEST_ASSERT_EQUAL_CHAR('c', constant.back());

    s.back() = 'C';
    TEST_ASSERT_EQUAL_CHAR('C', s.back());
    TEST_ASSERT_EQUAL_STRING("abC", s.c_str());
    TEST_ASSERT_EQUAL_UINT(3, s.size());
}

TEST_CASE("static_string: pop_back removes the last character",
          "[static_string]") {
    static_string<8> s("abc");

    s.pop_back();
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("ab", s.c_str());
    TEST_ASSERT_EQUAL_CHAR('\0', s[2]);
    TEST_ASSERT_EQUAL_UINT(6, s.remaining());

    s.pop_back();
    s.pop_back();
    TEST_ASSERT_TRUE(s.empty());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
    TEST_ASSERT_EQUAL_CHAR('\0', s[0]);
}

// --- Element access and iteration -------------------------------------------

TEST_CASE("static_string: indexing and iteration expose characters",
          "[static_string]") {
    static_string<8> s("abc");
    TEST_ASSERT_EQUAL_CHAR('a', s[0]);
    TEST_ASSERT_EQUAL_CHAR('c', s[2]);

    int count = 0;
    for (auto it = s.cbegin(); it != s.cend(); ++it) {
        ++count;
    }
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_PTR(s.begin(), s.data());
    TEST_ASSERT_EQUAL_PTR(s.cend(), s.data() + s.size());
}

// --- capacity()/remaining() -------------------------------------------------

TEST_CASE("static_string: remaining tracks free character slots",
          "[static_string]") {
    static_string<6> s("abc");
    TEST_ASSERT_EQUAL_UINT(3, s.remaining());
    s.append("def");
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
    TEST_ASSERT_TRUE(s.size() == s.capacity());
}

// --- Non-aborting try_push_back / try_append --------------------------------

TEST_CASE("static_string: try_push_back appends while there is room",
          "[static_string]") {
    static_string<2> s;
    TEST_ASSERT_TRUE(s.try_push_back('a'));
    TEST_ASSERT_TRUE(s.try_push_back('b'));
    TEST_ASSERT_EQUAL_STRING("ab", s.c_str());

    // Full: reports failure, stores nothing, and keeps the string valid.
    TEST_ASSERT_FALSE(s.try_push_back('c'));
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("ab", s.c_str());
}

TEST_CASE("static_string: try_append appends a bounded slice",
          "[static_string]") {
    static_string<5> s("ab");
    TEST_ASSERT_TRUE(s.try_append("cde", 3));
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());

    TEST_ASSERT_FALSE(s.try_append("f", 1));  // Full.
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
}

TEST_CASE("static_string: try_append is all-or-nothing", "[static_string]") {
    static_string<4> s("ab");
    // One character fits, but the whole slice does not: nothing is stored.
    TEST_ASSERT_FALSE(s.try_append("cde", 3));
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("ab", s.c_str());

    TEST_ASSERT_TRUE(s.try_append("cd", 2));  // The available characters.
    TEST_ASSERT_EQUAL_STRING("abcd", s.c_str());

    TEST_ASSERT_FALSE(s.try_append(nullptr, 3));  // Nothing to copy.
    TEST_ASSERT_EQUAL_STRING("abcd", s.c_str());
}

TEST_CASE("static_string: a zero-capacity static_string stays empty",
          "[static_string]") {
    // N == 0 holds only the terminator: nothing can be appended, ever.
    static_string<0> s;
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
    TEST_ASSERT_FALSE(s.try_push_back('x'));
    TEST_ASSERT_FALSE(s.try_append("x", 1));
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
}

TEST_CASE("static_string: append a bounded slice copies exactly n bytes",
          "[static_string]") {
    // The source is not NUL-terminated: only the first `n` bytes count.
    constexpr char raw[] = {'x', 'y', 'z', 'q'};
    static_string<8> s("ab");
    s.append(raw, 3);
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_EQUAL_STRING("abxyz", s.c_str());
}

// --- Comparison -------------------------------------------------------------

TEST_CASE("static_string: equal contents compare equal", "[static_string]") {
    static_string<8> a("abc");
    static_string<8> b("abc");
    static_string<3> tight("abc");
    static_string<8> empty;

    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a != b);

    // Capacity is not part of the value.
    TEST_ASSERT_TRUE(a == tight);
    TEST_ASSERT_TRUE(tight == a);
    TEST_ASSERT_TRUE(a == "abc");
    TEST_ASSERT_TRUE(empty == "");
    TEST_ASSERT_TRUE(empty == static_string<1>());
}

TEST_CASE("static_string: different contents compare unequal",
          "[static_string]") {
    static_string<8> abc("abc");

    TEST_ASSERT_TRUE(abc != static_string<8>("abd"));
    TEST_ASSERT_TRUE(abc != static_string<8>("ab"));
    TEST_ASSERT_TRUE(abc != static_string<8>("abcd"));
    TEST_ASSERT_TRUE(abc != "");
    TEST_ASSERT_TRUE(abc != "abcd");
    TEST_ASSERT_TRUE(abc != "ab");
}

TEST_CASE("static_string: a prefix sorts before the longer string",
          "[static_string]") {
    static_string<8> ab("ab");
    static_string<8> abc("abc");

    TEST_ASSERT_TRUE(ab < abc);
    TEST_ASSERT_TRUE(ab <= abc);
    TEST_ASSERT_TRUE(abc > ab);
    TEST_ASSERT_TRUE(abc >= ab);
    TEST_ASSERT_FALSE(abc < ab);
    TEST_ASSERT_FALSE(abc <= ab);

    // The empty string is the smallest.
    static_string<8> empty;
    TEST_ASSERT_TRUE(empty < ab);
    TEST_ASSERT_TRUE(empty <= ab);
    TEST_ASSERT_FALSE(ab < empty);
}

TEST_CASE("static_string: ordering is by character value", "[static_string]") {
    // Plain char comparison (strncmp), not collation: 'B' (0x42) < 'a' (0x61).
    TEST_ASSERT_TRUE(static_string<8>("B") < static_string<8>("a"));
    TEST_ASSERT_TRUE(static_string<8>("Z") < static_string<8>("a"));
    TEST_ASSERT_TRUE(static_string<8>("abc") < static_string<8>("abd"));

    // ... and it applies to a C string too.
    static_string<8> abc("abc");
    TEST_ASSERT_TRUE(abc < "abd");
    TEST_ASSERT_TRUE(abc > "abb");
    TEST_ASSERT_TRUE(abc <= "abc");
    TEST_ASSERT_TRUE(abc >= "abc");
}

TEST_CASE(
    "static_string: the three-way comparison agrees with the "
    "relational operators",
    "[static_string]") {
    static_string<8> abc("abc");

    TEST_ASSERT_TRUE((abc <=> static_string<8>("abc")) ==
                     std::strong_ordering::equal);
    TEST_ASSERT_TRUE((abc <=> static_string<8>("abd")) ==
                     std::strong_ordering::less);
    TEST_ASSERT_TRUE((abc <=> static_string<8>("abb")) ==
                     std::strong_ordering::greater);

    TEST_ASSERT_TRUE((abc <=> "abc") == std::strong_ordering::equal);
    TEST_ASSERT_TRUE((abc <=> "abd") == std::strong_ordering::less);
    TEST_ASSERT_TRUE((abc <=> "abb") == std::strong_ordering::greater);
}

TEST_CASE("static_string: a null C string compares as empty",
          "[static_string]") {
    static_string<8> empty;
    static_string<8> abc("abc");

    TEST_ASSERT_TRUE(empty == static_cast<const char*>(nullptr));
    TEST_ASSERT_FALSE(abc == static_cast<const char*>(nullptr));
    TEST_ASSERT_TRUE(abc != static_cast<const char*>(nullptr));
    TEST_ASSERT_TRUE((empty <=> static_cast<const char*>(nullptr)) ==
                     std::strong_ordering::equal);
    TEST_ASSERT_TRUE((abc <=> static_cast<const char*>(nullptr)) ==
                     std::strong_ordering::greater);
}

TEST_CASE("static_string: an embedded NUL is an ordinary character",
          "[static_string]") {
    // A bounded append can store a NUL inside the string. Comparison is by
    // size() and content -- like std::string, not like strcmp -- so the NUL
    // does not truncate the comparison, and `==` agrees with `<=>`.
    static_string<8> embedded;
    embedded.append("a\0b", 3);
    TEST_ASSERT_EQUAL_UINT(3, embedded.size());

    // A C string literal cannot *contain* an embedded NUL: "a\0b" is the
    // one-character string "a". So the C-string overload compares against
    // one character, and the longer value sorts after it.
    TEST_ASSERT_TRUE(embedded != "a");
    TEST_ASSERT_TRUE((embedded <=> "a") == std::strong_ordering::greater);
    TEST_ASSERT_TRUE(embedded < "b");

    // Against another static_string the NUL is just a character: same bytes
    // compare equal, and a shorter prefix sorts first.
    static_string<8> same;
    same.append("a\0b", 3);
    TEST_ASSERT_TRUE(embedded == same);
    TEST_ASSERT_TRUE((embedded <=> same) == std::strong_ordering::equal);

    static_string<3> prefix("a");
    TEST_ASSERT_TRUE(embedded != prefix);
    TEST_ASSERT_TRUE(embedded > prefix);
}

// --- Overflow asserts (fork + exec the standalone program) ------------------

namespace {

// Path to the sibling static_string_assert program (shared by the cases below).
const assert_test_support::AssertProgramPath& assert_program() {
    static const auto path =
        assert_test_support::assert_program_path("static_string_assert");
    return path;
}

}  // namespace

TEST_CASE("static_string: constructing past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "construct_overflow"));
}

TEST_CASE("static_string: string_view construction past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "construct_string_view_overflow"));
}

TEST_CASE("static_string: append past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "append_overflow"));
}

TEST_CASE("static_string: assign past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "assign_overflow"));
}

TEST_CASE("static_string: push_back when full aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "push_back_when_full"));
}

TEST_CASE("static_string: back when empty aborts", "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "back_when_empty"));
}

TEST_CASE("static_string: pop_back when empty aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "pop_back_when_empty"));
}
