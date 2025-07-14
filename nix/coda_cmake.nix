{ pkgs ? import <nixpkgs> { config.allowUnfree = true; } }:

with pkgs;

let
  python = python39;  # You can change this to python310 or another version if needed
in

stdenv.mkDerivation rec {
  pname = "coda";
  version = "2.25.2";

  src = fetchurl {
    url = "https://github.com/stcorp/coda/releases/download/${version}/CODA-${version}.tar.gz";
    sha256 = "sha256-qBaPa6ziSQkwPpQQh4lqGYw3lLZHk9QZS7g17IhSs4M=";
  };

  nativeBuildInputs = [
    cmake
    pkg-config
    flex bison
    doxygen
    swig
  ];

  buildInputs = [
    zlib
    hdf4
    hdf5
    szip
    python
    python.pkgs.setuptools
    python.pkgs.numpy
  ];

  # Most of these flags are ignored 
  cmakeFlags = [
    "-DCMAKE_INSTALL_PREFIX=$out"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_TOOLS=ON"
    "-DBUILD_PYTHON=ON"
    "-DPYTHON_EXECUTABLE=${python.interpreter}"
    "-DPYTHON_INCLUDE_DIR=${python}/include/${python.libPrefix}"
    "-DPYTHON_LIBRARY=${python}/lib/libpython${python.libPrefix}.so"
  ];

  # postInstall = ''
  #   echo "Installing Python wrapper to $out"
  #   mkdir -p $out/${python.sitePackages}
  #   cp -r python/coda $out/${python.sitePackages}/
  # '';

  meta = with lib; {
    description = "CODA: Library for scientific data access (NetCDF, HDF4/5) with Python bindings";
    homepage = "https://github.com/stcorp/coda";
    license = licenses.gpl2;
    platforms = platforms.unix;
  };
}

