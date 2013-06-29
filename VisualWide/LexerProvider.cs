using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Linq;
using System.Threading;
using System.Windows.Media;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Text.Operations;
using Microsoft.VisualStudio.Text.Tagging;
using Microsoft.VisualStudio.Utilities;
using System.Runtime.InteropServices;

namespace VisualWide
{
    public class LexerProvider
    {
        public static LexerProvider GetProviderForBuffer(ITextBuffer buf)
        {
            return buf.Properties.GetOrCreateSingletonProperty(typeof(LexerProvider), () => new LexerProvider(buf));
        }
        public enum Failure : int
        {
            UnterminatedStringLiteral,
            UnlexableCharacter,
            UnterminatedComment
        };
        public enum TokenType : int
        {
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

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private struct MaybeByte
        {
            public byte asciichar;
            public byte present;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct Position
        {
            public UInt32 column;
            public UInt32 line;
            public UInt32 offset;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct Range
        {
            public Position begin;
            public Position end;
        }
        
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate MaybeByte LexerCallback(System.IntPtr arg);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void RawCommentCallback(Range arg, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate byte RawErrorCallback(Position p, Failure f, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate byte RawTokenCallback(Range r, [MarshalAs(UnmanagedType.LPStr)]string s, TokenType type, System.IntPtr context);

        private delegate bool TokenCallback(Range loc, string s, TokenType t);
        private delegate bool ErrorCallback(Position p, Failure f);
        private delegate void CommentCallback(Range arg);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void LexWide(
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]LexerCallback callback,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawTokenCallback token,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawCommentCallback comment,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawErrorCallback error
        );

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern byte IsKeywordType(TokenType type);

        public static bool IsKeyword(TokenType t)
        {
            return IsKeywordType(t) == 1;
        }

        private static void Read(
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
                (where, value, type, context) =>
                {
                    var tok = new Token();
                    tok.location = where;
                    tok.value = value;
                    tok.type = type;
                    return token(where, value, type) ? (byte)1 : (byte)0;
                },
                (where, context) =>
                {
                    comment(where);
                },
                (pos, fail, context) =>
                {
                    return err(pos, fail) ? (byte)1 : (byte)0;
                }
            );
        }

        public struct Error
        {
            public Error(SnapshotSpan loc, Failure err)
            {
                where = loc;
                what = err;
            }
            public SnapshotSpan where;
            public Failure what;
        }

        public struct Token
        {
            public Token(SnapshotSpan span, Range r, string val, TokenType t)
            {
                where = span;
                location = r;
                type = t;
                value = val;
            }
            public SnapshotSpan where;
            public Range location;
            public TokenType type;
            public string value;
        }

        class ResultTypes
        {
            public List<SnapshotSpan> comments = new List<SnapshotSpan>();
            public List<Token> tokens = new List<Token>();
            public List<Error> errors = new List<Error>();
        }

        ITextBuffer TextBuffer;
        Dictionary<ITextSnapshot, ResultTypes> SnapshotResults = new Dictionary<ITextSnapshot, ResultTypes>();

        public delegate void ContentsChanged(SnapshotSpan span);

        public event ContentsChanged TagsChanged = delegate {};

        public IEnumerable<SnapshotSpan> GetComments(ITextSnapshot shot)
        {
            if (!SnapshotResults.ContainsKey(shot))
                LexSnapshot(shot);
            return SnapshotResults[shot].comments;
        }
        public IEnumerable<Token> GetTokens(ITextSnapshot shot)
        {
            if (!SnapshotResults.ContainsKey(shot))
                LexSnapshot(shot);
            return SnapshotResults[shot].tokens;
        }
        public IEnumerable<Error> GetErrors(ITextSnapshot shot)
        {
            if (!SnapshotResults.ContainsKey(shot))
                LexSnapshot(shot);
            return SnapshotResults[shot].errors;
        }

        LexerProvider(ITextBuffer buf) {
            TextBuffer = buf;

            buf.Changed += (sender, args) => {
                TagsChanged(new SnapshotSpan(args.After, new Span(0, args.After.Length)));
            };
        }

        public static Span SpanFromLexer(Range range)
        {
            return new Span((int)range.begin.offset, (int)(range.end.offset - range.begin.offset));
        }

        void LexSnapshot(ITextSnapshot shot)
        {
            if (SnapshotResults.ContainsKey(shot))
                return;
            var list = new ResultTypes();
            SnapshotResults[shot] = list;
            Read(
                shot.GetText(),
                (where, what) =>
                {
                    Span loc;
                    if (what == Failure.UnlexableCharacter)
                        loc = new Span((int)where.offset, 0);
                    else
                        loc = new Span(
                            (int)where.offset,
                            (int)shot.Length - (int)where.offset
                        );
                    list.errors.Add(new Error(new SnapshotSpan(shot, loc), what));
                    return false;
                },
                where =>
                {
                    // Clamp this so it doesn't go over the end when we add \n in the lexer.
                    where.end.offset = where.end.offset > shot.Length ? (uint)(shot.Length) : where.end.offset;
                    list.comments.Add(new SnapshotSpan(shot, SpanFromLexer(where)));
                },
                (where, what, type) =>
                {
                    list.tokens.Add(new Token(new SnapshotSpan(shot, SpanFromLexer(where)), where, what, type));
                    return false;
                }
            );
        }
    }
}
