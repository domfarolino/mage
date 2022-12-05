def magen_idl(name, srcs):
  # This is the Mage macro that generates C++ headers from magen IDL files. You
  # simply give it a list of magen IDL files, and it generates the corresponding
  # headers, and outputs a `cc_library()` rule with the supplied name that lets
  # other targets include the generated headers.
  #
  # We have to use the `Label()` constructor [1] in this macro because it is
  # used from both:
  #   1.) Inside of the mage project, when we locate packages with the `//mage`
  #       prefix.
  #   2.) By applications consuming mage
  #
  # Given (2), the macro would naturally get evaluated in the context of other
  # projects (since it is evaluated at a different phase than normal rules).
  # This means it would only work for external projects if we hard-coded the
  # prefix `@mage//mage`, but would fail inside of Mage itself, or if the
  # external project remapped mage to a different name in its WORKSPACE.
  #
  # Since we need the macro to be resilient to all of these cases, we can use
  # the `Label()` constructor to force its evaluation at the time when normal
  # labels are evaluated.
  #
  # [1]: https://bazel.build/extending/macros#label-resolution

  outputs = []
  cmds = []
  for src in srcs:
    output = src + ".h"
    outputs.append(output)
    cmds.append("python3 $(location %s) $(location %s) $(location %s)" % (Label("//mage/public/parser:gen"), src, output))

  native.genrule(
    name = "gen_headers",
    outs = outputs,
    tools = [Label("//mage/public/parser:gen")] + srcs,
    cmd = " && ".join(cmds)
  )

  native.cc_library(
    name = "include",
    hdrs = [":gen_headers"]
  )
