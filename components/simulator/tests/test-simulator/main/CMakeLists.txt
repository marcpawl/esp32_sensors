# `libsimulator` is the component under test. It is empty today, so there is
# nothing to exercise yet: this suite exists so the component is genuinely
# linked (not just named) and the first test has a home. `WHOLE_ARCHIVE` is
# required once tests are added, so their `TEST_CASE` registrations are linked
# into the final executable even though nothing references them.

idf_component_register(
    SRCS
        "test_main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        libsimulator
    PRIV_REQUIRES
        unity
    WHOLE_ARCHIVE
)
