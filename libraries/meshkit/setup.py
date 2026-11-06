# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os
import subprocess
import sys
import sysconfig
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

ROOT = Path(__file__).resolve().parent

class NativeBuild(build_ext):
    def build_extension(self, extension):
        output = Path(self.get_ext_fullpath(extension.name)).resolve().parent
        build = Path(self.build_temp).resolve() / "meshkit-native"
        options = ["-DMESHKIT_BUILD_QT=OFF", "-DMESHKIT_BUILD_TESTS=OFF",
                   "-DMESHKIT_BUILD_BENCHMARKS=OFF", "-DMESHKIT_BUILD_CLI=OFF",
                   "-DMESHKIT_BUILD_PYTHON=ON", "-DCMAKE_BUILD_TYPE=Release",
                   f"-DPython3_EXECUTABLE={sys.executable}",
                   f"-DMESHKIT_PYTHON_OUTPUT={output}"]
        if sys.platform == "darwin":
            minimum = os.environ.get("MACOSX_DEPLOYMENT_TARGET") or sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET") or "15.0"
            options.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={minimum}")
        subprocess.check_call(["cmake", "-S", str(ROOT), "-B", str(build), *options])
        subprocess.check_call(["cmake", "--build", str(build), "--config", "Release",
                               "--target", "meshkit_python", "--parallel", "4"])

setup(name="meshkit", version="0.2.0", description="Independent mixed meshing with terrain certificates and streaming I/O",
      long_description=(ROOT / "python/README.md").read_text(), long_description_content_type="text/markdown",
      license="Apache-2.0", license_files=["LICENSE", "NOTICE", "RELICENSING.md", "vendor/predicates/LICENSE"],
      python_requires=">=3.10", packages=["meshkit"], package_dir={"": "python"},
      ext_modules=[Extension("meshkit._meshkit", sources=[])], cmdclass={"build_ext": NativeBuild},
      extras_require={"test": ["numpy>=1.24"]}, zip_safe=False)
