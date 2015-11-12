#!/usr/bin/env sh
premake4 --llvm-path=/opt/wide/llvm.3.6.0 --boost-path=/opt/wide/boost_1_59_0 gmake 
cd Wide
make config=release64 SemanticTest --trace
