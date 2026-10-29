# SConstruct
#!/usr/bin/env python
import os

api_version = ARGUMENTS.get("api_version", "4.7")
env = SConscript("godot-cpp/SConstruct", {"api_version": api_version})
env.Tool("compilation_db")
env.CompilationDatabase("compile_commands.json")

# Build options for profiling
profile = ARGUMENTS.get("profile", "no") == "yes"
tracy = ARGUMENTS.get("tracy", "yes" if profile else "no") == "yes"

# Add your source files
env.Append(CPPPATH=["src/"])

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
    env.Append(CCFLAGS=["-g", "-fno-omit-frame-pointer"])

if tracy:
    env.Append(CPPDEFINES=["TRACY_ENABLE", "TRACY_ON_DEMAND"])
    env.Append(CPPPATH=["thirdparty/tracy/public"])
    sources.append("thirdparty/tracy/public/TracyClient.cpp")
    if env["platform"] == "windows":
        env.Append(LIBS=["ws2_32", "dbghelp"])
    else:
        env.Append(LIBS=["pthread", "dl"])

# Build the library
if env["platform"] == "windows":
    library = env.SharedLibrary(
        "bin/myrias.{}".format(env["platform"]),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "bin/libmyrias.{}.{}".format(env["platform"], env["arch"]),
        source=sources,
    )

Default(library)
