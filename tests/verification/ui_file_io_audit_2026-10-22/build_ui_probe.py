"""Reuse the existing GUI regression target's build flags and objects."""
import pathlib,shlex,subprocess
root=pathlib.Path(__file__).resolve().parent
build=root.parents[2]/"build"
compile=shlex.split((root/"existing_compile_command.txt").read_text())
if "ccache" in compile[0]: compile=compile[1:]
compile[compile.index("-o")+1]=str(root/"ui_probe.o")
compile[compile.index("-c")+1]=str(root/"ui_probe.cpp")
if "-MF" in compile: compile[compile.index("-MF")+1]=str(root/"ui_probe.d")
subprocess.run(compile,cwd=build,check=True)
link=shlex.split((root/"existing_link_command.txt").read_text())[2:-2]
old=next(s for s in link if s.endswith("/test_simulationoptions_roundtrip.cpp.o"))
link[link.index(old)]=str(root/"ui_probe.o")
link[link.index("-o")+1]=str(root/"ui_probe")
subprocess.run(link,cwd=build,check=True)
