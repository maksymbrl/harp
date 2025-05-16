{ pkgs ? import <nixpkgs> 
  {
    config = { allowUnfree = true; };
  }
}:

pkgs.stdenv.mkDerivation rec {
  pname = "coda";
  version = "2.25.2";

  src = pkgs.fetchurl {
    url = "https://github.com/stcorp/coda/releases/download/${version}/CODA-${version}.tar.gz";
    sha256 = "sha256-qBaPa6ziSQkwPpQQh4lqGYw3lLZHk9QZS7g17IhSs4M=";
  };

  nativeBuildInputs = with pkgs; [
    autoconf automake libtool
    flex bison
    pkg-config
    python39
    doxygen
    swig
    # autoreconfHook  # <<< this ensures configure is rebuilt 
  ];

  buildInputs = with pkgs; [
    zlib hdf4 hdf5 szip 
    stdenv.cc.libc 
  ];

  # To avoid error: 
  # 
  # > libtool:   error: only absolute run-paths are allowed
  # > make[1]: *** [Makefile:1669: libcoda.la] Error 1
  # > make[1]: Leaving directory '/build/coda-2.25.2'
  # > make: *** [Makefile:1261: all] Error 2
  # 
  # which occurs because Nix enforces pure builds, which disallow relative
  # paths in RPATH/RUNPATH. CODA’s libtool build is attempting to set a
  # relative RPATH (like -rpath ../lib), which Nix forbids. 
  # postConfigure = ''
  #   substituteInPlace libtool \
  #     --replace " -rpath " " -rpath $out/lib "
  # '';

  # Fix libtool relative RPATH issue before build starts
  # preBuild = ''
  #   echo "Patching libtool to use absolute rpath"
  #   substituteInPlace libtool \
  #     --replace " -rpath " " -rpath $out/lib "
  # '';
  
  # preConfigure = ''
  #   echo "" 
  #   echo "Patching relative -rpath to absolute"
  #   substituteInPlace configure.ac Makefile.in Makefile.am \
  #     --replace "-rpath ut/lib" "-rpath $out/lib"
  #   echo "" 
  # '';

  configureFlags = [
    # "--prefix=$out" 
    "--with-hdf4"
    "--with-hdf5"
    "--enable-python"
  ];



  # installPhase = ''
  #   make install
  #   echo "Installed CODA tools to $out"
  # '';

  meta = with pkgs.lib; {
    description = "CODA: library for scientific data formats (HDF4/5, NetCDF)";
    homepage = "https://github.com/stcorp/coda";
    license = licenses.gpl2;
    platforms = platforms.unix;
  };
}

