using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;

namespace W
{
    class Lexer : IDisposable {
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
        
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct ErrorToken {
            public Range location;
            public TokenType type; 
            IntPtr s;
            public string value { get { return Marshal.PtrToStringAnsi(s); } }
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
        public delegate void CommentCallback(Range arg);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate ErrorToken ErrorCallback(Position p, [MarshalAs(UnmanagedType.I4)]Failure f);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr CreateLexer(
            [MarshalAs(UnmanagedType.FunctionPtr)]LexerCallback callback,
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]CommentCallback comment,
            [MarshalAs(UnmanagedType.FunctionPtr)]ErrorCallback error
        );

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr GetToken(System.IntPtr lexer);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr GetTokenValue(System.IntPtr token);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern TokenType GetTokenType(System.IntPtr token);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern Range GetTokenLocation(System.IntPtr token);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DeleteToken(System.IntPtr token);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DeleteLexer(System.IntPtr lexer);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void ClearLexerState(System.IntPtr lexer);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern byte IsKeywordType(System.IntPtr lexer, TokenType type);

        public class LexerError : System.Exception
        {
            public Position where;
            public Failure what;
        }

        public bool IsKeyword(TokenType t)
        {
            if (NativeLexerHandle == IntPtr.Zero)
                throw new System.InvalidOperationException("Attempted to read from a post-disposed Lexer.");
            return IsKeywordType(NativeLexerHandle, t) == 1;
        }

        public void Dispose()
        {
            DeleteLexer(NativeLexerHandle);
            NativeLexerHandle = IntPtr.Zero;
        }

        string contents;
        IntPtr NativeLexerHandle;
        LexerCallback PreventLexerCallbackGC;
        CommentCallback PreventCommentCallbackGC;
        ErrorCallback PreventErrorCallbackGC;
        int offset = 0;
        System.Tuple<Position, Failure> error;

        public Lexer(String data, CommentCallback comment) {
            contents = data;
            PreventLexerCallbackGC = con =>
            {
                var ret = new MaybeByte();
                ret.present = (byte)(offset < contents.Length ? 1 : 0);
                if (ret.present == 1)
                {
                    ret.asciichar = (byte)contents[offset++];
                }
                return ret;                
            };
            PreventCommentCallbackGC = comment;
            PreventErrorCallbackGC = (Position p, Failure f) => {
                error = new System.Tuple<Position, Failure>(p, f);
                var ret = new ErrorToken();
                //ret.value = "Fail.";
                return ret;
            };
            NativeLexerHandle = CreateLexer(
                PreventLexerCallbackGC,
                System.IntPtr.Zero,
                PreventCommentCallbackGC,
                PreventErrorCallbackGC
            );
        }

        public void SetContents(String content)
        {
            if (NativeLexerHandle == IntPtr.Zero)
                throw new System.InvalidOperationException("Attempted to read from a post-disposed Lexer.");
            contents = content;
            offset = 0;
            ClearLexerState(NativeLexerHandle);
        }

        public Nullable<Token> Read()
        {
            if (NativeLexerHandle == IntPtr.Zero)
                throw new System.InvalidOperationException("Attempted to read from a post-disposed Lexer.");
            var result = GetToken(NativeLexerHandle);
            
            // Maybe error
            if (error != null)
            {
                DeleteToken(result);
                var except = new LexerError();
                except.what = error.Item2;
                except.where = error.Item1;
                error = null;
                throw except;
            }

            // Maybe just at the end
            if (result == System.IntPtr.Zero)
                return null;

            var retval = new Token();
            retval.value = Marshal.PtrToStringAnsi(GetTokenValue(result));
            retval.type = GetTokenType(result);
            retval.location = GetTokenLocation(result);
            DeleteToken(result);
            return retval;
        }
    }
}
