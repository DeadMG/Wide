call "C:\Program Files\Emscripten\emscripten\1.35.0\emcc" -s DISABLE_EXCEPTION_CATCHING=0 -I ./ --std=c++14 --bind --memory-init-file 0 -O3 -o Lexer.bc Wide/Lexer/Lexer.cpp
call "C:\Program Files\Emscripten\emscripten\1.35.0\emcc" -s DISABLE_EXCEPTION_CATCHING=0 -I ./ -I D:\Code\Boost --std=c++14 --bind --memory-init-file 0 -O3 -o Parser.bc Wide/Parser/Parser.cpp
call "C:\Program Files\Emscripten\emscripten\1.35.0\emcc" -s DISABLE_EXCEPTION_CATCHING=0 -I ./ -I D:\Code\Boost --std=c++14 --bind --memory-init-file 0 -O3 -o AST.bc Wide/Parser/AST.cpp
call "C:\Program Files\Emscripten\emscripten\1.35.0\emcc" -s DISABLE_EXCEPTION_CATCHING=0 --std=c++14 --memory-init-file 0 -O3 --bind Lexer.bc Parser.bc AST.bc -o Wide.js
del Lexer.bc
del Parser.bc
del AST.bc