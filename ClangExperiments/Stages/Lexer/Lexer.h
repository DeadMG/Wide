#pragma once

#include "Token.h"
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace Wide {
    namespace Lexer {
        class Token {
            Range location;
            TokenType type;
            std::string value;
        public:
            Token(Range r, TokenType t, std::string val)
                : location(r), type(t), value(std::move(val)) {}
            Range GetLocation() const { return location; }
            TokenType GetType() const { return type; }
            std::string GetValue() const { return value; }
        };
        class Arguments {
        public:
            enum Failure {
                UnterminatedStringLiteral,
                AccessedPastEnd,
                UnlexableCharacter,
                UnterminatedComment
            };
            std::unordered_map<char, TokenType> singles;
            std::unordered_map<char, std::unordered_map<char, TokenType>> doubles;
            std::unordered_set<char> whitespace;
            std::unordered_map<std::string, TokenType> keywords;
            std::function<Token(Position, Failure)> OnError;
            std::function<void(Range)> OnComment;
            int tabsize;
            Arguments()
                : tabsize(4) 
            {
                OnError = [](Position, Failure) -> Token {
                    throw std::runtime_error("Fuckshitballs.");
                };
                OnComment = [](Range) {};

                singles[';'] = TokenType::Semicolon;
                singles['.'] = TokenType::Dot;
                singles['}'] = TokenType::CloseCurlyBracket;
                singles['{'] = TokenType::OpenCurlyBracket;
                singles[')'] = TokenType::CloseBracket;
                singles['('] = TokenType::OpenBracket;
                singles[','] = TokenType::Comma;
                singles['='] = TokenType::Assignment;
                singles['!'] = TokenType::Exclaim;
                singles['|'] = TokenType::Or;
                singles['^'] = TokenType::Xor;
                singles['&'] = TokenType::And;
                singles['<'] = TokenType::LT;
                singles['>'] = TokenType::GT;
                
                doubles['<']['<'] = TokenType::LeftShift;
                doubles['>']['>'] = TokenType::RightShift;
                doubles[':']['='] = TokenType::VarCreate;
                doubles['=']['='] = TokenType::EqCmp;
                doubles['!']['='] = TokenType::NotEqCmp;
                doubles['<']['='] = TokenType::LTE;
                doubles['>']['='] = TokenType::GTE;
        
                whitespace.insert('\r');
                whitespace.insert('\t');
                whitespace.insert('\n');
                whitespace.insert(' ');
        
                keywords["return"] = TokenType::Return;
                keywords["using"] = TokenType::Using;
                keywords["prolog"] = TokenType::Prolog;
                keywords["module"] = TokenType::Module;
                keywords["if"] = TokenType::If;
                keywords["else"] = TokenType::Else;
                keywords["while"] = TokenType::While;
            }
            template<typename Iterator, typename Cont> Position increment(Position& p, Iterator& i, Cont&& cont) {
                Position out(p);
                if (*i == '\n') {
                    ++p.line;
                    p.column = 1;
                } else {
                    p.column += (*i == '\t' ? tabsize : 1);
                }
                ++p.offset;
                if (!cont.empty())
                    cont.pop_back();
                else
                    ++i;
                return p;
            }
        };
        template<typename Iterator> class Invocation {
            Arguments* args;
            mutable Iterator current_iterator;
            Iterator end;
            mutable Position current_position;
            std::vector<Token> putbacks;

            mutable std::vector<typename std::iterator_traits<Iterator>::value_type> iterator_putbacks;
            
            typename std::iterator_traits<Iterator>::value_type current() const {
                return iterator_putbacks.empty() ? *current_iterator : iterator_putbacks.back();
            }

            bool RecursiveParseCComments() const {
                // Don't track location in a comment, so nobody gives a shitnibbles about position.
                while(true) {
                    if (current_iterator == end) {
                        args->OnError(current_position, Arguments::Failure::UnterminatedComment);
                        return false;
                    }
                    // If we matched another /* pair, recurse.
                    if (current() == '/') {
                        args->increment(current_position, current_iterator, iterator_putbacks);
                        if (current_iterator == end) {
                            args->OnError(current_position, Arguments::Failure::UnterminatedComment);
                            return false;
                        }
                        if (current() == '*') {
                            args->increment(current_position, current_iterator, iterator_putbacks);
                            if (!RecursiveParseCComments()) return false;
                        }
                    }
                    // If we matched */, stop.
                    if (current() == '*') {
                        args->increment(current_position, current_iterator, iterator_putbacks);
                        if (current_iterator== end) {
                            args->OnError(current_position, Arguments::Failure::UnterminatedComment);
                            return false;
                        }
                        if (current() == '/') {
                            args->increment(current_position, current_iterator, iterator_putbacks);
                            return true;
                        }
                    }
                    // Else, just keep going.
                    args->increment(current_position, current_iterator, iterator_putbacks);
                }
            }
        public:
            Invocation(Arguments& arg, Iterator begin, Iterator last)
                : args(&arg), current_iterator(std::move(begin)), end(std::move(last)) {}
            std::string escape(std::string val) {
                std::string result;
                for(auto begin = val.begin(); begin != val.end(); ++begin) {
                    if (*begin == '\\' && begin + 1 != val.end()) {
                        switch(*(begin + 1)) {
                        case 'n':
                            result.push_back('\n');
                            ++begin;
                            continue;
                        case 'r':
                            result.push_back('\r');
                            ++begin;
                            continue;
                        case 't':
                            result.push_back('\t');
                            ++begin;
                            continue;
                        }
                    }
                    result.push_back(*begin);
                }
                return result;
            }
            operator bool() const {
                // If we're at the end, there are no more tokens.
                if (current_iterator== end) {
                    return !putbacks.empty();
                }

                // Read forward to find a non-whitespace character (or end) to deal with trailing whitespace.
                if (args->whitespace.find(current()) != args->whitespace.end()) {
                    args->increment(current_position, current_iterator, iterator_putbacks);
                    return (bool)(*this);
                }

                // Check for comments
                if (current() == '/') {
                    auto prev = args->increment(current_position, current_iterator, iterator_putbacks);
                    if (current() == '/') {
                        while(current() != '\n') {
                            args->increment(current_position, current_iterator, iterator_putbacks);
                            if (current_iterator == end) {
                               args->OnError(current_position, Arguments::Failure::UnterminatedComment);
                               return false;
                            }
                        }
                        args->OnComment(prev + current_position);
                        // Found a comment- go again because there may be something or indeed nothing after it
                        return (bool)(*this);
                    }
                    if (current() == '*') {
                        args->increment(current_position, current_iterator, iterator_putbacks);
                        if (!RecursiveParseCComments()) return false;
                        args->OnComment(prev + current_position);
                        return (bool)(*this);
                    }
                    // Fuckles, it wasn't a comment, so putback
                    current_position = prev;
                    iterator_putbacks.push_back(current());
                }
                // We found something- will either error or return token, so operator() is good to go.
                return true;
            }
            // Reverse order. If we call lex(t1), lex(t2), then lex() lex() should give t2, t1
            void operator()(Token t) {
                putbacks.push_back(t);
            }
            Token operator()() {
                if (!*this)
                    return args->OnError(current_position, Arguments::Failure::AccessedPastEnd);

                // If there's a putback read it back before lexing a new one.
                if (!putbacks.empty()) {
                    auto t = putbacks.back();
                    putbacks.pop_back();
                    return t;
                }
        
                // Maximal munch- doubles first
                if (args->doubles.find(current()) != args->doubles.end()) {
                    auto&& newsingles = args->doubles[current()];
                    auto curr = *current_iterator;
                    auto begin = args->increment(current_position, current_iterator, iterator_putbacks);      
                    if (current_iterator == end)
                        return args->OnError(current_position, Arguments::Failure::AccessedPastEnd);              
                    if (newsingles.find(current()) != newsingles.end()) {
                        auto it = current_iterator;
                        args->increment(current_position, current_iterator, iterator_putbacks);
                        Token t(begin + current_position, newsingles[*it], std::string(current_iterator - 2, it));
                        return t;
                    } else {
                        iterator_putbacks.push_back(curr);
                        current_position = begin;
                    }
                }
                if (args->singles.find(current()) != args->singles.end()) {
                    Token t(current_position, args->singles[current()], std::string(current_iterator, current_iterator+ 1));
                    args->increment(current_position, current_iterator, iterator_putbacks);
                    return t;
                }
                // Ident, string, or number.
                if (current() == '"') {
                    // string
                    auto begin = current_iterator;
                    auto start = args->increment(current_position, current_iterator, iterator_putbacks);
                    if (current_iterator == end)
                        return args->OnError(current_position, Arguments::Failure::AccessedPastEnd);
                    while(current() != '"') {
                        if (current_iterator== end)
                            return args->OnError(current_position, Arguments::Failure::UnterminatedStringLiteral);
                        args->increment(current_position, current_iterator, iterator_putbacks);
                    }
                    auto end = current_iterator;
                    args->increment(current_position, current_iterator, iterator_putbacks);
                    return Token(start + current_position, TokenType::String, escape(std::string(begin+1, end)));
                }
                if (current() >= '0' && current() <= '9' && current_iterator!= end) {
                    auto type = TokenType::Integer;
                    auto begin = current_iterator;
                    auto start = current_position;
                    while(current() >= '0' && current() <= '9' && current_iterator != end) {
                        args->increment(current_position, current_iterator, iterator_putbacks);
                    }
                    return Token(start + current_position, type, std::string(begin, current_iterator));
                }
                // ident
                auto currpos = current_position;
                auto currit = current_iterator;
                while(((current() >= 'a' && current() <= 'z') || (current() >= 'A' && current() <= 'Z') || current() == '_' || (current() >= '0' && current() <= '9')) && (current_iterator != end)) {
                    args->increment(current_position, current_iterator, iterator_putbacks);
                }
                auto val = std::string(currit, current_iterator);
                auto type = TokenType::Identifier;
                if (args->keywords.find(val) != args->keywords.end())
                    type = args->keywords[val];
                Token t(currpos + current_position, type, std::move(val));
                if (currpos == current_position) {
                    args->increment(current_position, current_iterator, iterator_putbacks);
                    return args->OnError(currpos, Arguments::Failure::UnlexableCharacter);
                }
                return t;
            }
        };
    }
}