// Generated from the Metadata serializer and aggregator; see
// golden_events.h. Strings with quotes, backslashes or control characters
// were changed by hand when string escaping was added.
#ifndef DFTRACER_TEST_GOLDEN_LINES_H
#define DFTRACER_TEST_GOLDEN_LINES_H
// clang-format off
namespace golden {
// [include args][line_events() index]
static const char* const kData[2][9] = {
    {
        "{\"id\":7,\"name\":\"open\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":100,\"dur\":5,\"ph\":1}",
        "{\"id\":7,\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":110,\"dur\":6,\"ph\":1}",
        "{\"id\":7,\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":120,\"dur\":7,\"ph\":1}",
        "{\"id\":7,\"name\":\"step\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":2,\"ts\":130,\"dur\":8,\"ph\":1 | \"hhash\":\"hosthash\" \"relations\":[{\"rel\":\"used\",\"id\":\"e1\"}]",
        "{\"id\":7,\"name\":\"numbers\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":3,\"ts\":140,\"dur\":0,\"ph\":1}",
        "{\"id\":7,\"name\":\"strings\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":3,\"ts\":150,\"dur\":1,\"ph\":1}",
        "{\"id\":7,\"name\":\"lists\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":4,\"ts\":160,\"dur\":2,\"ph\":1 | \"hhash\":\"hosthash\" \"relations\":{\"used\":[\"0000000000000001\",\"ffffffffffffffff\"],\"made\":[\"x\\\"y\"]}",
        "{\"id\":7,\"name\":\"only_ignored\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":170,\"dur\":3,\"ph\":1}",
        "{\"id\":7,\"name\":\"say \\\"x\\\"\",\"cat\":\"c\\\\at\\u000a\",\"type\":6,\"pid\":42,\"tid\":5,\"ts\":180,\"dur\":2,\"ph\":1}",
    },
    {
        "{\"id\":7,\"name\":\"open\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":100,\"dur\":5,\"ph\":1}",
        "{\"id\":7,\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":110,\"dur\":6,\"ph\":1 | \"hhash\":\"hosthash\"",
        "{\"id\":7,\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":120,\"dur\":7,\"ph\":1 | \"bw\":1.5e+09 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"mode\":\"r\" \"note\":\"x\" \"offset\":1099511627776 \"size\":-4096",
        "{\"id\":7,\"name\":\"step\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":2,\"ts\":130,\"dur\":8,\"ph\":1 | \"epoch\":2 \"hhash\":\"hosthash\" \"relations\":[{\"rel\":\"used\",\"id\":\"e1\"}]",
        "{\"id\":7,\"name\":\"numbers\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":3,\"ts\":140,\"dur\":0,\"ph\":1 | \"big\":1e+21 \"frac\":1.23457e+08 \"hhash\":\"hosthash\" \"i64_max\":9223372036854775807 \"i64_min\":-9223372036854775808 \"neg\":-2.5 \"tenth\":0.1 \"tiny\":1e-07 \"u64_max\":18446744073709551615 \"whole\":3 \"zero\":0",
        "{\"id\":7,\"name\":\"strings\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":3,\"ts\":150,\"dur\":1,\"ph\":1 | \"control\":\"tab\\u0009line\\u000aret\\u000d\\u0001\" \"empty\":\"\" \"hhash\":\"hosthash\" \"ptr\":\"pointer\" \"quote\":\"say \\\"hi\\\"\" \"raw\":{\"a\":[1,2],\"b\":null} \"slash\":\"a\\\\b/c\" \"utf8\":\"caf\303\251\"",
        "{\"id\":7,\"name\":\"lists\",\"cat\":\"app\",\"type\":6,\"pid\":42,\"tid\":4,\"ts\":160,\"dur\":2,\"ph\":1 | \"hhash\":\"hosthash\" \"items\":[\"a\",-3,\"0123456789abcdef\"] \"relations\":{\"used\":[\"0000000000000001\",\"ffffffffffffffff\"],\"made\":[\"x\\\"y\"]}",
        "{\"id\":7,\"name\":\"only_ignored\",\"cat\":\"POSIX\",\"type\":3,\"pid\":42,\"tid\":1,\"ts\":170,\"dur\":3,\"ph\":1 | \"hhash\":\"hosthash\" \"note\":1",
        "{\"id\":7,\"name\":\"say \\\"x\\\"\",\"cat\":\"c\\\\at\\u000a\",\"type\":6,\"pid\":42,\"tid\":5,\"ts\":180,\"dur\":2,\"ph\":1 | \"hhash\":\"hosthash\" \"k\\\"ey\":\"v\\u0009al\" \"n\\\\m\":1",
    },
};
// [include args][line_events() index]
static const char* const kCounter[2][9] = {
    {
        "{\"name\":\"open\",\"cat\":\"POSIX\",\"type\":3,\"ts\":100,\"ph\":2,\"pid\":42,\"tid\":1}",
        "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":110,\"ph\":2,\"pid\":42,\"tid\":1}",
        "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":120,\"ph\":2,\"pid\":42,\"tid\":1 | \"bw\":1.5e+09 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"mode\":\"r\" \"note\":\"x\" \"offset\":1099511627776 \"size\":-4096",
        "{\"name\":\"step\",\"cat\":\"app\",\"type\":6,\"ts\":130,\"ph\":2,\"pid\":42,\"tid\":2 | \"epoch\":2 \"hhash\":\"hosthash\" \"relations\":[{\"rel\":\"used\",\"id\":\"e1\"}]",
        "{\"name\":\"numbers\",\"cat\":\"app\",\"type\":6,\"ts\":140,\"ph\":2,\"pid\":42,\"tid\":3 | \"big\":1e+21 \"frac\":1.23457e+08 \"hhash\":\"hosthash\" \"i64_max\":9223372036854775807 \"i64_min\":-9223372036854775808 \"neg\":-2.5 \"tenth\":0.1 \"tiny\":1e-07 \"u64_max\":18446744073709551615 \"whole\":3 \"zero\":0",
        "{\"name\":\"strings\",\"cat\":\"app\",\"type\":6,\"ts\":150,\"ph\":2,\"pid\":42,\"tid\":3 | \"control\":\"tab\\u0009line\\u000aret\\u000d\\u0001\" \"empty\":\"\" \"hhash\":\"hosthash\" \"ptr\":\"pointer\" \"quote\":\"say \\\"hi\\\"\" \"raw\":{\"a\":[1,2],\"b\":null} \"slash\":\"a\\\\b/c\" \"utf8\":\"caf\303\251\"",
        "{\"name\":\"lists\",\"cat\":\"app\",\"type\":6,\"ts\":160,\"ph\":2,\"pid\":42,\"tid\":4 | \"hhash\":\"hosthash\" \"items\":[\"a\",-3,\"0123456789abcdef\"] \"relations\":{\"used\":[\"0000000000000001\",\"ffffffffffffffff\"],\"made\":[\"x\\\"y\"]}",
        "{\"name\":\"only_ignored\",\"cat\":\"POSIX\",\"type\":3,\"ts\":170,\"ph\":2,\"pid\":42,\"tid\":1 | \"hhash\":\"hosthash\" \"note\":1",
        "{\"name\":\"say \\\"x\\\"\",\"cat\":\"c\\\\at\\u000a\",\"type\":6,\"ts\":180,\"ph\":2,\"pid\":42,\"tid\":5 | \"hhash\":\"hosthash\" \"k\\\"ey\":\"v\\u0009al\" \"n\\\\m\":1",
    },
    {
        "{\"name\":\"open\",\"cat\":\"POSIX\",\"type\":3,\"ts\":100,\"ph\":2,\"pid\":42,\"tid\":1}",
        "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":110,\"ph\":2,\"pid\":42,\"tid\":1}",
        "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":120,\"ph\":2,\"pid\":42,\"tid\":1 | \"bw\":1.5e+09 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"mode\":\"r\" \"note\":\"x\" \"offset\":1099511627776 \"size\":-4096",
        "{\"name\":\"step\",\"cat\":\"app\",\"type\":6,\"ts\":130,\"ph\":2,\"pid\":42,\"tid\":2 | \"epoch\":2 \"hhash\":\"hosthash\" \"relations\":[{\"rel\":\"used\",\"id\":\"e1\"}]",
        "{\"name\":\"numbers\",\"cat\":\"app\",\"type\":6,\"ts\":140,\"ph\":2,\"pid\":42,\"tid\":3 | \"big\":1e+21 \"frac\":1.23457e+08 \"hhash\":\"hosthash\" \"i64_max\":9223372036854775807 \"i64_min\":-9223372036854775808 \"neg\":-2.5 \"tenth\":0.1 \"tiny\":1e-07 \"u64_max\":18446744073709551615 \"whole\":3 \"zero\":0",
        "{\"name\":\"strings\",\"cat\":\"app\",\"type\":6,\"ts\":150,\"ph\":2,\"pid\":42,\"tid\":3 | \"control\":\"tab\\u0009line\\u000aret\\u000d\\u0001\" \"empty\":\"\" \"hhash\":\"hosthash\" \"ptr\":\"pointer\" \"quote\":\"say \\\"hi\\\"\" \"raw\":{\"a\":[1,2],\"b\":null} \"slash\":\"a\\\\b/c\" \"utf8\":\"caf\303\251\"",
        "{\"name\":\"lists\",\"cat\":\"app\",\"type\":6,\"ts\":160,\"ph\":2,\"pid\":42,\"tid\":4 | \"hhash\":\"hosthash\" \"items\":[\"a\",-3,\"0123456789abcdef\"] \"relations\":{\"used\":[\"0000000000000001\",\"ffffffffffffffff\"],\"made\":[\"x\\\"y\"]}",
        "{\"name\":\"only_ignored\",\"cat\":\"POSIX\",\"type\":3,\"ts\":170,\"ph\":2,\"pid\":42,\"tid\":1 | \"hhash\":\"hosthash\" \"note\":1",
        "{\"name\":\"say \\\"x\\\"\",\"cat\":\"c\\\\at\\u000a\",\"type\":6,\"ts\":180,\"ph\":2,\"pid\":42,\"tid\":5 | \"hhash\":\"hosthash\" \"k\\\"ey\":\"v\\u0009al\" \"n\\\\m\":1",
    },
};
// aggregated_events(), sorted
static const char* const kAggregated[] = {
    "{\"name\":\"close\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"dft_cnt\":4 \"dur_max\":1 \"dur_min\":1 \"dur_sum\":4 \"hhash\":\"hosthash\"",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"bbb\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":2 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":2 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"bbb\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":10000000000,\"ph\":3,\"pid\":42,\"tid\":1 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":10000000000,\"ph\":3,\"pid\":42,\"tid\":1 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"bbb\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":10000000000,\"ph\":3,\"pid\":42,\"tid\":2 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"aaa\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"read\",\"cat\":\"POSIX\",\"type\":3,\"ts\":10000000000,\"ph\":3,\"pid\":42,\"tid\":2 | \"bw_max\":1 \"bw_min\":0.25 \"bw_sum\":2.5 \"dft_cnt\":4 \"dur_max\":13 \"dur_min\":10 \"dur_sum\":46 \"fd\":3 \"fhash\":\"bbb\" \"hhash\":\"hosthash\" \"offset_max\":25769803776 \"offset_min\":0 \"offset_sum\":51539607552 \"size_max\":400 \"size_min\":100 \"size_sum\":1000",
    "{\"name\":\"seek\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":3 | \"delta\":3 \"dft_cnt\":1 \"dur\":2 \"frac\":0.75 \"hhash\":\"hosthash\" \"whence\":\"set\"",
    "{\"name\":\"seek\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":3 | \"delta_max\":-1 \"delta_min\":-5 \"delta_sum\":-6 \"dft_cnt\":2 \"dur_max\":2 \"dur_min\":2 \"dur_sum\":4 \"frac_max\":-0.25 \"frac_min\":-1.25 \"frac_sum\":-1.5 \"hhash\":\"hosthash\" \"whence\":\"cur\"",
    "{\"name\":\"stat\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"dft_cnt\":1 \"dur\":4 \"hhash\":\"hosthash\"",
    "{\"name\":\"stat\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"dft_cnt\":1 \"dur\":6 \"fhash\":\"ccc\" \"hhash\":\"hosthash\"",
    "{\"name\":\"we\\\"ird\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"dft_cnt\":1 \"dur\":4 \"f\\\"h\":\"a\\u000ab\" \"hhash\":\"hosthash\" \"s\\\\z\":5",
    "{\"name\":\"write\",\"cat\":\"POSIX\",\"type\":3,\"ts\":0,\"ph\":3,\"pid\":42,\"tid\":1 | \"bw\":2 \"dft_cnt\":1 \"dur\":9 \"hhash\":\"hosthash\" \"size\":7",
};
}  // namespace golden
// clang-format on
#endif
