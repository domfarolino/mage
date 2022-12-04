# This filegroup target consists of misc binaries that //mage:mage_tests depend
# on being built.
filegroup(
  name = "mage_tests_misc",
  srcs = [
    "//mage/test:invitee_as_remote",
    "//mage/test:inviter_as_remote",
    "//mage/test:inviter_as_remote_block_on_acceptance",
    "//mage/test:child_as_receiver_and_callback",
    "//mage/test:child_as_queued_handle_sender",
    "//mage/test:child_as_handle_sender",
    "//mage/test:child_sends_accept_invitation_pipe_to_parent",
    "//mage/test:child_sends_send_invitation_pipe_to_parent",
    "//mage/test:child_sends_two_pipes_to_parent",
    "//mage/test:pass_pipe_back_and_forth",
  ],
)

# See https://stackoverflow.com/a/57396491 for info about this.
config_setting(
  name = "set_bazel_test_directive",
  values = {
    "define": "BAZEL_TEST=true"
  }
)

cc_test(
  name = "mage_tests",
  srcs = [
    "//mage/core:mage_test.cc",
  ],
  copts = ["-Iexternal/gtest/include"],
  defines = select({
    ":set_bazel_test_directive": ["BAZEL_TEST"],
    "//conditions:default": [],
  }),
  deps = [
    "@base//base",
    "//mage/core",
    "//mage/public:include",
    "//mage/test/magen:include",
    "@gtest//:gtest",
    "@gtest//:gtest_main",
  ],
  data = ["//mage:mage_tests_misc"],
)
