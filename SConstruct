#!/usr/bin/env python
import os

godot_cpp_path = "src/lib/godot-cpp"
steam_audio_path = "src/lib/steamaudio"
if (not (os.path.isdir(godot_cpp_path) and os.listdir(godot_cpp_path))) or (not (os.path.isdir(steam_audio_path) and os.listdir(steam_audio_path))):
    print("""Git submodule dependencies are missing.
    Run the following command to install them:
    git submodule update --init --recursive""")
    Exit(1)

env = SConscript("src/lib/godot-cpp/SConstruct")

steam_audio_lib_path = env.get("STEAM_AUDIO_LIB_PATH", "src/lib/steamaudio/lib")
if not (os.path.isdir(steam_audio_lib_path) and os.listdir(steam_audio_lib_path)):
    print("""No valid Steam Audio library path was found.
    Run the following command to install the latest release:
    make install-steam-audio""")
    Exit(1)

# Use the headers installed with the prebuilt SDK, not the Unity plugin headers.
# CPPPATH also lets SCons track SDK header changes and rebuild dependent objects.
env.Append(CPPPATH=["src/", "src/lib/steamaudio/include/"])

sources = Glob("src/*.cpp")

env.Append(LIBS=["phonon"])

if env["platform"] == "linux":
    env.Append(LIBPATH=[f'{steam_audio_lib_path}/linux-x64'])
    env.Append(LINKFLAGS=["-Wl,--version-script={}".format(env.File("linux_symbols.map").abspath)])
elif env["platform"] == "windows":
    env.Append(LIBPATH=[f'{steam_audio_lib_path}/windows-x64'])
elif env["platform"] == "macos":
    env.Append(LIBPATH=[f'{steam_audio_lib_path}/osx'])
    env.Append(LINKFLAGS=['-Wl,-rpath,@loader_path'])
elif env["platform"] == "android":
    if env["arch"] == "arm64":
        env.Append(LIBPATH=[f'{steam_audio_lib_path}/android-armv8'])
    if env["arch"] == "arm32":
        env.Append(LIBPATH=[f'{steam_audio_lib_path}/android-armv7'])
    if env["arch"] == "x86_64":
        env.Append(LIBPATH=[f'{steam_audio_lib_path}/android-x64'])
    if env["arch"] == "x86_32":
        env.Append(LIBPATH=[f'{steam_audio_lib_path}/android-x32'])
elif env["platform"] == "ios":
    env.Append(LIBPATH=[f'{steam_audio_lib_path}/ios'])

if env["target"] in ["editor", "template_debug"]:
    sources += Glob("src/editor/*.cpp")
    doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
    sources.append(doc_data)

if env["platform"] == "ios":
    library = env.StaticLibrary(
        "project/addons/godot-steam-audio/bin/libgodot-steam-audio{}{}".format(env["suffix"], env["LIBSUFFIX"]),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "project/addons/godot-steam-audio/bin/libgodot-steam-audio{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)

test_env = env.Clone()
test_env["LINKFLAGS"] = [f for f in test_env.get("LINKFLAGS", []) if "version-script" not in str(f)]
test_env.Replace(LIBS=["phonon"])
if env["platform"] == "linux":
    test_env.Append(LINKFLAGS=["-Wl,-rpath,{}".format(env.Dir(f"{steam_audio_lib_path}/linux-x64").abspath)])
probe_test = test_env.Program("tests/probe_batch_test", ["tests/probe_batch_test.cpp", "src/probe_core.cpp"])
Alias("test", probe_test)
