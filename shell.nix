let
  nixpkgs_24_11 = import (fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/nixos-24.11.tar.gz";
    sha256 = "sha256:1s2gr5rcyqvpr58vxdcb095mdhblij9bfzaximrva2243aal3dgx"; 
  }) { config = { allowUnfree = true; }; };

  pkgs = nixpkgs_24_11; 
  # coda = import ./nix/coda.nix { inherit pkgs; };
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
    # source code formatter 
    indent

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

