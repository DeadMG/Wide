using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;

namespace VisualWide
{
    class Lexer {
        public enum Failure : int {
            UnterminatedStringLiteral,
            UnlexableCharacter,
            UnterminatedComment
        };
        public enum TokenType : int {
            OpenBracket,
            CloseBracket,
            Dot,
            Semicolon,
            Identifier,
            String,
            LeftShift,
            RightShift,
            OpenCurlyBracket,
            CloseCurlyBracket,
            Return,
            Assignment,
            VarCreate,
            Comma,
            Integer,
            Using,
            Prolog,
            Module,
            If,
            Else,
            EqCmp,
            Exclaim,
            While,
            NotEqCmp,
            This,
            Type,
            Operator,
            Function,
            OpenSquareBracket,
            CloseSquareBracket,
            Colon,
            Dereference,
            PointerAccess,
            Negate,
            Plus,
            Increment,
            Decrement,
            Minus,
        
            LT,
            LTE,
            GT,
            GTE,
            Or,
            And,
            Xor
        }
        
        [StructLayout(LayoutKind.Sequential, Pack=8)]
        private struct MaybeByte {
            public byte asciichar;
            public byte present;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct Position {
            public UInt32 column;
            public UInt32 line;
            public UInt32 offset;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct Range {
            public Position begin;
            public Position end;
        }
        
        public struct Token
        {
            public Range location;
            public TokenType type;
            public string value;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate MaybeByte LexerCallback(System.IntPtr arg);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void RawCommentCallback(Range arg, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate byte RawErrorCallback(Position p, Failure f, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate byte RawTokenCallback(Range r, [MarshalAs(UnmanagedType.LPStr)]string s, TokenType type, System.IntPtr context);

        public delegate bool TokenCallback(Token t);
        public delegate bool ErrorCallback(Position p, Failure f);
        public delegate void CommentCallback(Range arg);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void LexWide(
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]LexerCallback callback,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawCommentCallback comment,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawErrorCallback error,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawTokenCallback token
        );
        
        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern byte IsKeywordType(TokenType type);
        
        public bool IsKeyword(TokenType t)
        {
            return IsKeywordType(t) == 1;
        }

        public void Read(
            System.String contents,
            ErrorCallback err, 
            CommentCallback comment,
            TokenCallback token
            )
        {
            if (!contents.EndsWith("\n"))
                contents += "\n";
            int offset = 0;
            LexWide(
                System.IntPtr.Zero,
                (context) =>
                {
                    var ret = new MaybeByte();
                    ret.present = (byte)(offset < contents.Length ? 1 : 0);
                    if (ret.present == 1)
                    {
                        ret.asciichar = (byte)contents[offset++];
                    }
                    return ret;
                },
                (where, context) =>
                {
                    comment(where);
                },
                (pos, fail, context) =>
                {
                    return err(pos, fail) ? (byte)1 : (byte)0;
                },
                (where, value, type, context) =>
                {
                    var tok = new Token();
                    tok.location = where;
                    tok.value = value;
                    tok.type = type;
                    return token(tok) ? (byte)1 : (byte)0;
                }
            );
        }
    }
}
