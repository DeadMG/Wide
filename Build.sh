#!/usr/bin/env sh
premake4 --boost-path=/opt/wide/boost_1_59_0 gmake 
cd Wide
make CXX=clang-3.7 config=release64 SemanticTest
