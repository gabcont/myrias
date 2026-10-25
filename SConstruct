# SConstruct
#!/usr/bin/env python
import os

env = SConscript("godot-cpp/SConstruct")
env.Tool("compilation_db")
env.CompilationDatabase("compile_commands.json")

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
