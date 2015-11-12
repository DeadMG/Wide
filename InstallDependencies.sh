#!/usr/bin/env sh
sudo apt-get update -qq
sudo apt-get install -y premake4
sudo apt-get install -y libarchive-dev
sudo apt-get install -y zlib1g-dev
sudo apt-get install -y ncurses-dev

sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update -q
sudo apt-get install gcc-4.8 -y

if [ "$CXX" = "g++" ]; then export CXX="g++-4.8" CC="gcc-4.8"; fi
$CXX --version 

if ! test -e "/opt/wide" ; then
    sudo mkdir /opt/wide
fi

if ! test -e "/opt/wide/boost_1_59_0" ; then
    echo "Didn't find boost 1.59, installing..."
    wget http://sourceforge.net/projects/boost/files/boost/1.59.0/boost_1_59_0.tar.bz2
    tar --bzip2 -xf boost_1_59_0.tar.bz2
    rm boost_1_59_0.tar.bz2
    cd boost_1_59_0
    ./bootstrap.sh
    sudo ./b2 link=static threading=multi runtime-link=static --with-program_options
    cd ..
    sudo mv boost_1_59_0 /opt/wide/boost_1_59_0
else
    echo "Boost 1.59 already found."
fi

if ! test -e "/opt/wide/llvm.3.6.0" ; then
    echo "Didn't find LLVM 3.6.0, installing..."
    wget http://llvm.org/releases/3.6.0/llvm-3.6.0.src.tar.xz
    tar -axf llvm-3.6.0.src.tar.xz
    rm llvm-3.6.0.src.tar.xz
    mv llvm-3.6.0.src llvm
    wget http://llvm.org/releases/3.6.0/cfe-3.6.0.src.tar.xz
    tar -axf cfe-3.6.0.src.tar.xz
    rm cfe-3.6.0.src.tar.xz
    mv cfe-3.6.0.src clang
    mv clang llvm/tools
    sudo mv llvm /opt/wide/llvm.3.6.0
    cd /opt/wide/llvm.3.6.0
    sudo CXX = $CXX ./configure --enable-optimized --enable-assertions
    sudo CXX = $CXX REQUIRES_RTTI=1 make
else
    echo "LLVM 3.6.0 already found."
fi



