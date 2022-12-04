load("@rules_cc//cc:defs.bzl", "cc_library")
load("//mage/public/parser:magen_idl.bzl", "magen_idl")

package(default_visibility = ["//visibility:public"])

magen_idl(
  name = "include",
  srcs = [
    "callback_interface.magen",
    "first_interface.magen",
    "fourth_interface.magen",
    "second_interface.magen",
    "test.magen",
    "third_interface.magen",
    "handle_accepter.magen",
  ],
)
