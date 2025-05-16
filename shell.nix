{ pkgs ? import <nixpkgs> 
  {
    config = { allowUnfree = true; };
  }
}:

let
  coda = import ./nix/coda.nix { inherit pkgs; };
in

pkgs.mkShell {
  name = "dev-shell-with-coda";

  buildInputs = with pkgs; [
    # Compilers and build tools
    gcc
    cmake
    autoconf automake libtool
    flex bison
    doxygen swig graphviz 
    zip

    # Libraries
    zlib szip hdf4 hdf5
    libjpeg 

    # Custom CODA package
    coda

    # Python
    python39

    #
    netcdf 
  ];

  shellHook = ''
    echo "Development shell with CODA 2.25.2 and all required build tools."
  '';
}

