from jinja2 import Template

import hashlib
import os
import sys

######################################################################## HELPERS
class Method:
  def __init__(self, name, arguments):
    self.name = name
    self.arguments = arguments

def GetInterfaceName(interface_descriptor_string):
  return interface_descriptor_string.split()[1]

def GetListOfMethods(method_descriptors):
  return_list = []
  for method_descriptor in method_descriptors:
    method_descriptor = method_descriptor.replace(',', '')
    # Pull the method name
    split = method_descriptor.split()
    method_name = split[0]
    split = split[1:]

    # Pull the types and names
    types = split[::2]
    names = split[1::2]
    print(types, names)
    # See https://stackoverflow.com/questions/13651965/ for why we have to wrap
    # list(zip(...)) below.
    return_list.append(Method(method_name, list(zip(types, names))));
  return return_list
#################################################################### END HELPERS

print(sys.argv[1:]) # For debugging

################################################################## START PARSING
# Parse the source.
source_location = sys.argv[1:][0]
source = open(source_location, "r")

source_text = source.read()
source_text = source_text.replace("(", " ")
source_text = source_text.replace(")", " ")

# Separate out "..... interface <NameHere> {" from the rest of the interface
# descriptor.
separate_interface_from_methods = source_text.split("{")
# Note that right now, we do not supoort multiple interfaces in a single file.
assert len(separate_interface_from_methods) == 2

# Take all of the "..... interface <NameHere>" stuff, and split by new line to
# get rid of all preceding comments, and only take the non-comment portion.
interface_plus_name = separate_interface_from_methods[0].split("\n")[-1]
interface_descriptor = [interface_plus_name] + separate_interface_from_methods[1].split("\n")

# The interface might have comments in it. Remove those lines for parsing.
lines_to_remove = []
for line in interface_descriptor:
  if line == "" or "//" in line:
    lines_to_remove.append(line)
for line in lines_to_remove:
  interface_descriptor.remove(line)

# Remove the trailing "}" from the descriptor
assert interface_descriptor[-1] == '}'
interface_descriptor.pop()

for i in range(len(interface_descriptor)):
  # I'm not sure if this is needed... Keep it just in case?
  interface_descriptor[i] = interface_descriptor[i].replace(";", " ");
  interface_descriptor[i] = interface_descriptor[i].strip(' \n\t')

print(interface_descriptor)
interface_name = GetInterfaceName(interface_descriptor[0])
list_of_methods = GetListOfMethods(interface_descriptor[1:])
#################################################################### END PARSING

source.close()

##################################################### HELPERS USED IN TEMPLATING
def GetNativeType(magen_type):
  if magen_type == "string":
    return "std::string"
  elif magen_type == "MessagePipe":
    return "mage::MessagePipe"
  return magen_type

def GetMagenParamsType(magen_type):
  if magen_type == "string":
    return "mage::Pointer<mage::ArrayHeader<char>>"
  elif magen_type == "MessagePipe":
    return "mage::EndpointDescriptor"
  return magen_type

def IsArrayType(magen_type):
  if magen_type == "string":
    return True
  return False

def GetArrayPrimitiveType(magen_type):
  if magen_type == "string":
    return "char"
  assert false

def IsHandleType(magen_type):
  if magen_type == "MessagePipe":
    return True
  return False

def MethodIDHash(method_name):
  return abs(hash(method_name)) % (10 ** 8)
################################################# END HELPERS USED IN TEMPLATING

# Generate a C++ header from the parsed source parameters.

with open(os.path.join(sys.path[0], "classes.tmpl-created")) as f:
  generated_magen_template = Template(f.read())

generated_magen_template = generated_magen_template.render(
                             Interface=interface_name,
                             Methods=list_of_methods,
                             GetNativeType=GetNativeType,
                             GetMagenParamsType=GetMagenParamsType,
                             IsArrayType=IsArrayType,
                             GetArrayPrimitiveType=GetArrayPrimitiveType,
                             IsHandleType=IsHandleType,
                             MethodIDHash=MethodIDHash,
                           )

destination = sys.argv[1:][1]
text_file = open(destination, "w")
text_file.writelines(generated_magen_template)
text_file.writelines('\n')
text_file.close()

print("OK, the header has been generated")
