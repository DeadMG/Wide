#!/usr/bin/env sh
sudo add-apt-repository ppa:codegear/release -y
sudo apt-get update -q
sudo apt-get install -y premake4 
sudo apt-get install -y libarchive-dev
sudo apt-get install -y zlib1g-dev
sudo apt-get install -y ncurses-dev

if ! test -e "/opt/wide" ; then
    sudo mkdir /opt/wide
fi

if ! test -e "/opt/wide/boost_1_59_0" ; then
    echo "Didn't find boost 1.59, installing..."
    wget http://sourceforge.net/projects/boost/files/boost/1.59.0/boost_1_59_0.tar.bz2
    tar --bzip2 -xf boost_1_59_0.tar.bz2
    rm boost_1_59_0.tar.bz2
    cd boost_1_59_0
    ./bootstrap.sh --prefix=/usr/local
    sudo ./b2 link=static threading=multi runtime-link=static --with-program_options install
else
    echo "Boost 1.59 already found."
fi




