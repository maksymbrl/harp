{ pkgs ? import <nixpkgs> { config.allowUnfree = true; } }:

with pkgs;

stdenv.mkDerivation rec {
  pname = "coda";
  version = "2.25.2";

  src = fetchurl {
    url = "https://github.com/stcorp/coda/releases/download/${version}/CODA-${version}.tar.gz";
    sha256 = "sha256-qBaPa6ziSQkwPpQQh4lqGYw3lLZHk9QZS7g17IhSs4M=";
  };

  nativeBuildInputs = [
    autoconf automake libtool
    pkg-config flex bison
    doxygen swig
    python39
  ];

  buildInputs = [
    zlib hdf4 hdf5 szip
    python39.pkgs.setuptools
    python39.pkgs.numpy
  ];

  configureFlags = [
    "--prefix=$out"
    "--with-hdf4"
    "--with-hdf5"
    "--enable-python"      # comment this out if you only want CLI
    # "--disable-shared"     # avoids libtool rpath issues
  ];

  enableParallelBuilding = true;


  # preConfigure = ''
  #   echo "Fixing libtool relative RPATH in libcoda Makefile.in..."
  #   substituteInPlace Makefile.am \
  #     --replace "-rpath ut/lib" "-rpath $out/lib"
  # '';

  # preConfigure = ''
  #   echo "Fixing libtool relative RPATH in libcoda..."
  #   substituteInPlace Makefile.am \
  #     --replace "-rpath ut/lib" "-rpath $out/lib"
  #   autoreconf -fi
  # '';

  # preConfigure = ''
  #   # No autoreconf needed: CODA tarball is prebootstrapped
  #   ./configure ${lib.concatStringsSep " " configureFlags}
  # '';

  # buildPhase = ''
  #   echo "Fixing relative RPATH in generated Makefile..."
  #   substituteInPlace Makefile \
  #     --replace "-rpath ut/lib" "-rpath $out/lib"

  #   make
  # '';



    meta = with lib; {
      description = "CODA: library for accessing remote sensing and scientific data formats";
      homepage = "https://github.com/stcorp/coda";
      license = licenses.gpl2;
      platforms = platforms.unix;
    };
  }

