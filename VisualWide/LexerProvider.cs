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
    class LexerProvider
    {
        public struct Error
        {
            public Error(SnapshotSpan loc, Lexer.Failure err)
            {
                where = loc;
                what = err;
            }
            public SnapshotSpan where;
            public Lexer.Failure what;
        }

        public struct Token
        {
            public Token(SnapshotSpan span, Lexer.Token tok)
            {
                where = span;
                what = tok;
            }
            public SnapshotSpan where;
            public Lexer.Token what;
        }

        public class ResultTypes
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

        public LexerProvider(ITextBuffer buf) {
            TextBuffer = buf;

            buf.Changed += (sender, args) => {
                LexSnapshot(args.After);
                TagsChanged(new SnapshotSpan(args.After, new Span(0, args.After.Length)));
            };
        }

        Span SpanFromLexer(Lexer.Range range)
        {
            return new Span((int)range.begin.offset, (int)(range.end.offset - range.begin.offset));
        }

        void LexSnapshot(ITextSnapshot shot)
        {
            if (SnapshotResults.ContainsKey(shot))
                return;
            var list = new ResultTypes();
            SnapshotResults[shot] = list;
            Lexer.Read(
                shot.GetText(),
                (where, what) =>
                {
                    Span loc;
                    if (what == Lexer.Failure.UnlexableCharacter)
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
                token =>
                {
                    list.tokens.Add(new Token(new SnapshotSpan(shot, SpanFromLexer(token.location)), token));
                    return false;
                }
            );
        }
    }
}
