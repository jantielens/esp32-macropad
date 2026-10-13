"""Unpack an isolated Octave runtime; never commit apt system changes."""

import json
import os
from pathlib import Path
import subprocess

import apt
import apt_pkg

OUTPUT = Path(__file__).resolve().parent
PREFIX = Path(__file__).resolve().parents[3] / "build/temp/heart/experiment/octave-local"


def main():
    apt_pkg.config.set("APT::Install-Recommends", "false")
    apt_pkg.config.set("APT::Install-Suggests", "false")
    cache = apt.Cache()
    cache["octave"].mark_install()
    cache["octave-signal"].mark_install()
    cache["octave-statistics"].mark_install()
    packages = cache.get_changes()
    archive = PREFIX / "archives"
    archive.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {len(packages)} packages, {sum(package.candidate.size for package in packages) / 1e6:.1f} MB", flush=True)
    versions = {}
    for package in packages:
        candidate = package.candidate
        filename = candidate.fetch_binary(str(archive))
        subprocess.run(["dpkg-deb", "--extract", filename, str(PREFIX)], check=True)
        versions[package.name] = candidate.version
    (PREFIX / "packages.json").write_text(json.dumps(versions, indent=2) + "\n")
    libraries = sorted({path.parent for path in (PREFIX / "usr/lib").rglob("*.so*")})
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = ":".join(str(path) for path in libraries if path.is_dir())
    environment["OCTAVE_HOME"] = str(PREFIX / "usr")
    environment["PERL5LIB"] = str(PREFIX / "usr/share/perl5")
    subprocess.run([str(PREFIX / "usr/bin/octave-cli"), "--no-gui", "--quiet", "--eval",
                    f"addpath(genpath('{PREFIX}/usr/share/octave/packages')); disp(version); assert(length(downsample(1:100,4))==25); assert(rows(hist3([0,0;1,1],[4,4]))==4);"],
                   env=environment, check=True)
    (PREFIX / "environment.json").write_text(json.dumps(
        {key: environment[key] for key in ["LD_LIBRARY_PATH", "OCTAVE_HOME", "PERL5LIB"]}, indent=2) + "\n")


if __name__ == "__main__":
    main()