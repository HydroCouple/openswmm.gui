"""Stage the arm64 face worker and its dynamically loaded AdaptiveCpp backends."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from verify_macos_bundle import commands


def resolve(name, loader, rpaths, worker):
    def expand(path):
        return Path(path.replace("@loader_path", str(loader.parent))
                    .replace("@executable_path", str(worker.parent)))

    if name.startswith("@rpath/"):
        for path in rpaths:
            candidate = expand(path) / name[len("@rpath/"):]
            if candidate.is_file():
                return candidate
    else:
        path = expand(name)
        if path.is_file():
            return path
    raise RuntimeError(f"{loader}: cannot resolve {name}")


def stage(worker, bundle):
    worker = worker.resolve()
    worker_loads, worker_paths = commands(worker)
    runtime = next(name for name in worker_loads if name.endswith("/libacpp-rt.dylib"))
    lib = resolve(runtime, worker, worker_paths, worker).parent.resolve()
    dest = bundle / "Contents/Frameworks/OpenSWMMMetal/lib"
    binary = bundle / "Contents/MacOS" / worker.name
    binary.parent.mkdir(parents=True, exist_ok=True)
    plugins = Path("hipSYCL")
    # AdaptiveCpp also requires its host backend during initialization.
    pending = [(worker, binary)] + [(lib / plugins / name, dest / plugins / name) for name in
                                    ("librt-backend-omp.dylib", "librt-backend-metal.dylib")]
    staged = {}
    while pending:
        src, target = pending.pop()
        src = src.resolve(strict=True)
        if target in staged:
            if staged[target] != src:
                raise RuntimeError(f"Conflicting libraries for {target}: {src} and {staged[target]}")
            continue
        staged[target] = src
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, target)
        target.chmod(0o755)
        loads, rpaths = commands(src)
        edits = []
        for path in dict.fromkeys(rpaths):
            edits += ["-delete_rpath", path]
        if target != binary:
            edits += ["-id", "@rpath/" + target.name]
        for name in dict.fromkeys(loads):
            if name.startswith(("/usr/lib/", "/System/Library/")):
                continue
            dep = resolve(name, src, rpaths + worker_paths, worker)
            # Keep the backend / translator layout used by AdaptiveCpp's discovery.
            try:
                relative = dep.parent.resolve().relative_to(lib) / dep.name
            except ValueError:
                relative = Path(dep.name)
            install = dest / relative
            pending.append((dep, install))
            edits += ["-change", name, "@loader_path/" + os.path.relpath(install, target.parent)]
        if edits:
            subprocess.run(["install_name_tool", *edits, target], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", target], check=True)
    bitcode = lib / plugins / "bitcode"
    if not (bitcode / "libkernel-sscp-metal-full.bc").is_file():
        raise RuntimeError(f"Missing Metal device library in {bitcode}")
    shutil.copytree(bitcode, dest / plugins / "bitcode", dirs_exist_ok=True)
    print(f"Staged {len(staged)} Metal worker binaries in {bundle}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    stage(args.worker, args.bundle.resolve())
