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
  commit = "b1b83638f2ae966516cd92db71e229ad6ee92031",
  remote = "https://github.com/domfarolino/base.git",
)
