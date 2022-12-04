load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

http_archive(
  name = "gtest",
  url = "https://github.com/google/googletest/archive/release-1.11.0.zip",
  sha256 = "353571c2440176ded91c2de6d6cd88ddd41401d14692ec1f99e35d013feda55a",
  strip_prefix = "googletest-release-1.11.0",
)

# This is sketchy, we should make this better.
git_repository(
  name = "base",
  commit = "ddd2ffb8dddb110be8a33c00fa579d4926b3cc5c",
  remote = "https://github.com/domfarolino/browser.git",
  shallow_since = "1670085830 -0700",
)
