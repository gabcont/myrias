# SConstruct
#!/usr/bin/env python
import os

api_version = ARGUMENTS.get("api_version", "4.7")
env = SConscript("godot-cpp/SConstruct", {"api_version": api_version})
extension_env = env.Clone()
extension_env.Tool("compilation_db")
extension_env.CompilationDatabase("compile_commands.json")

# Build options for profiling
profile = ARGUMENTS.get("profile", "no") == "yes"
tracy = ARGUMENTS.get("tracy", "yes" if profile else "no") == "yes"

# Add your source files
extension_env.Append(CPPPATH=["src/", "."])

def get_all_cpp_files(base_dir):
    cpp_files = []
    for root, dirs, files in os.walk(base_dir):
        for file in files:
            if file.endswith(".cpp"):
                cpp_files.append(os.path.join(root, file))
    return cpp_files

src_dir = "src/"
sources = get_all_cpp_files(src_dir)

if profile:
    extension_env.Append(CCFLAGS=["-g", "-fno-omit-frame-pointer"])
    extension_env["LINKFLAGS"] = [f for f in extension_env["LINKFLAGS"] if f not in ["-s", "-Wl,-s"]]

if tracy:
    extension_env.Append(CPPDEFINES=["TRACY_ENABLE", "TRACY_ON_DEMAND"])
    extension_env.Append(CPPPATH=["thirdparty/tracy/public"])
    sources.append("thirdparty/tracy/public/TracyClient.cpp")
    if extension_env["platform"] == "windows":
        extension_env.Append(LIBS=["ws2_32", "dbghelp"])
    else:
        extension_env.Append(LIBS=["pthread", "dl"])

extension_env.Append(CPPPATH=["thirdparty/highway"])
sources.extend([
    "thirdparty/highway/hwy/abort.cc",
    "thirdparty/highway/hwy/aligned_allocator.cc",
    "thirdparty/highway/hwy/per_target.cc",
    "thirdparty/highway/hwy/targets.cc",
])

# Build the library
if extension_env["platform"] == "windows":
    library = extension_env.SharedLibrary(
        "bin/myrias.{}".format(extension_env["platform"]),
        source=sources,
    )
else:
    library = extension_env.SharedLibrary(
        "bin/libmyrias.{}.{}".format(extension_env["platform"], extension_env["arch"]),
        source=sources,
    )

Default(library)
