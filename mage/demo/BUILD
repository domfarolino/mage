load("@rules_cc//cc:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
  name = "parent",
  srcs = ["parent.cc"],
  deps = [
    "@base//base",
    "//mage/demo/magen:include",
    "//mage/public",
  ],
)
cc_binary(
  name = "child",
  srcs = ["child.cc"],
  deps = [
    "@base//base",
    "//mage/demo/magen:include",
    "//mage/public",
  ],
)
