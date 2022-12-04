def magen_idl(name, srcs):
  # This is the Mage macro that generates C++ headers from magen IDL files. You
  # simply give it a list of magen IDL files, and it generates the corresponding
  # headers, and outputs a `cc_library()` rule with the supplied name that lets
  # other targets include the generated headers.

  outputs = []
  cmds = []
  for src in srcs:
    output = src + ".h"
    outputs.append(output)
    cmds.append("python3 $(location //mage/public/parser:gen) $(location %s) $(location %s)" % (src, output))

  native.genrule(
    name = "gen_headers",
    outs = outputs,
    tools = ["//mage/public/parser:gen"] + srcs,
    cmd = " && ".join(cmds)
  )

  native.cc_library(
    name = "include",
    hdrs = [":gen_headers"]
  )
