#!/usr/bin/env sh

if ! test -e "Tests" ; then
    mkdir Tests
    cp Wide/Build/x64/Release/SemanticTest Tests
    cp -r Wide/SemanticTest/. Tests
fi
cd Tests
./SemanticTest
cd ..
rm -rf Tests
