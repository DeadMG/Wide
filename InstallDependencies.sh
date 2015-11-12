#!/usr/bin/env sh
sudo apt-get update -qq
sudo apt-get install -y libarchive-dev
sudo apt-get install -y zlib1g-dev
sudo apt-get install -y ncurses-dev
sudo add-apt-repository 'deb http://llvm.org/apt/precise/ llvm-toolchain-precise main' -y
sudo add-apt-repository 'deb-src http://llvm.org/apt/precise/ llvm-toolchain-precise main' -y
wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key|sudo apt-key add -
sudo apt-get update -qq
sudo apt-get install -y llvm-3.6-dev
sudo apt-get install -y libclang-3.6-dev
sudo apt-get install -y clang-3.7
sudo add-apt-repository ppa:codegear/release -y
sudo apt-get update -q
sudo apt-get install -y premake4 

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




