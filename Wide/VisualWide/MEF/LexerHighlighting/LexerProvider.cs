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
        public enum BracketType : int
        {
            None,
            Open,
            Close
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private struct MaybeByte
        {
            public byte asciichar;
            public byte present;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct CPosition
        {
            public System.IntPtr location;
            public UInt32 column;
            public UInt32 line;
            public UInt32 offset;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct CRange
        {
            public CPosition begin;
            public CPosition end;
        }

        public struct Position
        {
            public System.String location;
            public UInt32 column;
            public UInt32 line;
            public UInt32 offset;
        }

        public struct Range
        {
            public Position begin;
            public Position end;
        }
        
        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int GetBracketNumber(System.IntPtr lexer, System.IntPtr type);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern BracketType GetBracketType(System.IntPtr lexer, System.IntPtr type);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern byte IsLiteral(System.IntPtr lexer, System.IntPtr type);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern byte IsKeyword(System.IntPtr lexer, System.IntPtr type);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate byte RawTokenCallback(CRange r, [MarshalAs(UnmanagedType.LPStr)]string s, System.IntPtr type, System.IntPtr lexer, System.IntPtr context);

        private delegate bool TokenCallback(CRange loc, string s, System.IntPtr type, System.IntPtr lexer);
        private delegate bool ErrorCallback(Position p, Failure f);
        private delegate void CommentCallback(CRange arg);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate MaybeByte LexerCallback(System.IntPtr arg);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void RawCommentCallback(CRange arg, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate byte RawErrorCallback(Position p, Failure f, System.IntPtr con);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void LexWide(
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]LexerCallback callback,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawTokenCallback token,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawCommentCallback comment,
            [MarshalAs(UnmanagedType.FunctionPtr)]RawErrorCallback error,
            [MarshalAs(UnmanagedType.LPStr)]String filename
        );
        
        private static void Read(
            System.String contents,
            ErrorCallback err,
            CommentCallback comment,
            TokenCallback token,
            System.String filename
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
                (where, value, type, lexer, context) =>
                {
                    return token(where, value, type, lexer) ? (byte)1 : (byte)0;
                },
                (where, context) =>
                {
                    comment(where);
                },
                (pos, fail, context) =>
                {
                    return err(pos, fail) ? (byte)1 : (byte)0;
                },
                filename
            );
        }

        public struct Error
        {
            public Error(SnapshotSpan loc, Failure err)
            {
                where = loc;
                what = err;
            }
            public SnapshotSpan where { get; private set; }
            public Failure what { get; private set; }
        }

        public struct Token
        {
            public Token(SnapshotSpan span, Range r, string val, System.IntPtr type, System.IntPtr lexer)
            {
                SpanLocation = span;
                SourceLocation = r;
                Type = type;
                Value = val;

                IsLiteral = LexerProvider.IsLiteral(lexer, type) == 1;
                IsKeyword = LexerProvider.IsKeyword(lexer, type) == 1;
                BracketNumber = GetBracketNumber(lexer, type);
                BracketType = GetBracketType(lexer, type);                
            }
            public SnapshotSpan SpanLocation { get; private set; }
            public Range SourceLocation { get; private set; }
            public bool IsLiteral { get; private set; }
            public bool IsKeyword { get; private set; }
            public BracketType BracketType { get; private set; }
            public string Value { get; private set; }
            public int BracketNumber { get; private set; }
            public System.IntPtr Type { get; private set; }
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
            ProjectUtils.SatisfyMEFImports(this);
            TextBuffer = buf;

            buf.Changed += (sender, args) => {
                TagsChanged(new SnapshotSpan(args.After, new Span(0, args.After.Length)));
            };
        }

        public static Span SpanFromLexer(Range range)
        {
            return new Span((int)range.begin.offset, (int)(range.end.offset - range.begin.offset));
        }
        public static Span SpanFromLexer(CRange range)
        {
            return SpanFromLexer(RangeFromCRange(range));
        }
        public static Range RangeFromCRange(CRange range)
        {
            Range r;
            r.begin.location = Marshal.PtrToStringAnsi(range.end.location);
            r.end.location = Marshal.PtrToStringAnsi(range.end.location);
            r.begin.offset = range.begin.offset;
            r.begin.line = range.begin.line;
            r.begin.column = range.begin.column;
            r.end.offset = range.end.offset;
            r.end.line = range.end.line;
            r.end.column = range.end.column;
            return r;
        }

        [Import]
        public ITextDocumentFactoryService factory = null;

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
                    // Account for the fact that VS does not properly handle this shit.
                    where.end.offset = where.end.offset > shot.Length ? (uint)(shot.Length) : where.end.offset;
                    list.comments.Add(new SnapshotSpan(shot, SpanFromLexer(where)));
                },
                (where, value, type, lexer) =>
                {
                    list.tokens.Add(new Token(new SnapshotSpan(shot, SpanFromLexer(where)), RangeFromCRange(where), value, type, lexer));
                    return false;
                },
                ProjectUtils.GetFileName(factory, shot.TextBuffer)
                //utils.GetFileName(shot.TextBuffer)
            );
        }
    }
}
