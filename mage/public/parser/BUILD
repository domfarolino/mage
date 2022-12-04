# DO NOT USE ANY OF THESE RULES DIRECTLY.
# They are only to be used by the macro in `magen_idl.bzl`, and as such
# unfortunately have to be marked as publicly visible.
package(default_visibility = ["//visibility:public"])

# A dumb rule just to expose `gen.py` to other targets.
# This stupid target has to have an output file, and for some reason it can't be
# the same as the input file. So basically we copy the input file to a new
# (idential) file with a different name, and then targets that use this
# `genrule` can actually run the `gen.py` file (really they run the
# `gen.py-created` file).
genrule(
    name = "gen",
# List :classes as a source, because `gen.py(-created)` runs on the template
# emitted by :classes.
    srcs = [":classes"],
    tools = ["gen.py", "classes.tmpl"],
    outs = ["gen.py-created"],
    cmd = "cp $(location gen.py) $@"
)
genrule(
    name = "classes",
    srcs = ["classes.tmpl"],
    outs = ["classes.tmpl-created"],
    cmd = "cp $(location classes.tmpl) $@"
)
