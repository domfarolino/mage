load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
  name = "core",
  hdrs = [
    "channel.h",
    "core.h",
    "endpoint.h",
    "node.h",
  ],
  srcs = [
    "api.cc",
    "channel.cc",
    "core.cc",
    "endpoint.cc",
    "message.cc",
    "node.cc",
    "util.cc",
  ],
  deps = [
    "@base//base",
    "//mage/public:include",
    "@gtest//:gtest_prod",
  ],
  visibility = [
    "//mage/public:__pkg__",
    "//mage:__pkg__",
  ],
)

exports_files([
  "core_unittest.cc",
  "mage_test.cc",
])
