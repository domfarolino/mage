load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

http_archive(
  name = "gtest",
  url = "https://github.com/google/googletest/archive/release-1.11.0.zip",
  sha256 = "353571c2440176ded91c2de6d6cd88ddd41401d14692ec1f99e35d013feda55a",
  strip_prefix = "googletest-release-1.11.0",
)

# This is sketchy, we should factor the `//base` library into its own repository
# and properly depend on a real version of it.
git_repository(
  name = "base",
  commit = "b672f15a716ab22d538fb4d2f2c80923d0456982",
  remote = "https://github.com/domfarolino/base.git",
  shallow_since = "1671417080 -0500",
)
