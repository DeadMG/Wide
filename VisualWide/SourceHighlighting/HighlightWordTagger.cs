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
    namespace SourceHighlighting
    {
        internal class HighlightWordTagger : ITagger<ClassificationTag>
        {
            ITextBuffer TextBuffer;
            IClassificationType Keyword;
            IClassificationType Comment;
            IClassificationType Literal;

            // Probably a giant memory leak
            Dictionary<ITextSnapshot, List<TagSpan<ClassificationTag>>> SnapshotResults = new Dictionary<ITextSnapshot, List<TagSpan<ClassificationTag>>>();

            public HighlightWordTagger(ITextBuffer sourceBuffer, IClassificationTypeRegistryService typeService)
            {
                TextBuffer = sourceBuffer;

                TextBuffer.Changed += (sender, args) =>
                {
                    LexSnapshot(args.After);

                    TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(args.After, new Span(0, args.After.Length))));
                };
                Keyword = typeService.GetClassificationType("WideKeyword");
                Comment = typeService.GetClassificationType("WideComment");
                Literal = typeService.GetClassificationType("WideLiteral");
            }

            public IEnumerable<ITagSpan<ClassificationTag>> GetTags(NormalizedSnapshotSpanCollection spans)
            {
                LexSnapshot(spans[0].Snapshot);
                foreach (var snapshotspan in SnapshotResults[spans[0].Snapshot])
                {
                    foreach (var span in spans)
                    {
                        if (snapshotspan.Span.IntersectsWith(span))
                        {
                            yield return snapshotspan;
                        }
                    }
                }
            }

            Span SpanFromLexer(Lexer.Range range)
            {
                return new Span((int)range.begin.offset, (int)(range.end.offset - range.begin.offset));
            }

            void LexSnapshot(ITextSnapshot shot)
            {
                if (SnapshotResults.ContainsKey(shot))
                    return;

                var lexer = new Lexer();
                var list = new List<TagSpan<ClassificationTag>>();
                SnapshotResults[shot] = list;
                lexer.Read(
                    shot.GetText(),
                    (where, what) =>
                    {
                        if (what == Lexer.Failure.UnlexableCharacter)
                            return false;
                        var loc = new Span(
                            (int)where.offset,
                            (int)shot.Length - (int)where.offset
                        );
                        if (what == Lexer.Failure.UnterminatedComment)
                            list.Add(new TagSpan<ClassificationTag>(new SnapshotSpan(shot, loc), new ClassificationTag(Comment)));
                        if (what == Lexer.Failure.UnterminatedStringLiteral)
                            list.Add(new TagSpan<ClassificationTag>(new SnapshotSpan(shot, loc), new ClassificationTag(Literal)));
                        return false;
                    }, 
                    where =>
                    {
                        // Clamp this so it doesn't go over the end when we add \n in the lexer.
                        where.end.offset = where.end.offset > shot.Length ? (uint)(shot.Length) : where.end.offset;
                        var loc = SpanFromLexer(where);
                        list.Add(new TagSpan<ClassificationTag>(new SnapshotSpan(shot, loc), new ClassificationTag(Comment)));
                    },
                    token => {
                        var location = SpanFromLexer(token.location);
                        if (token.type == Lexer.TokenType.String || token.type == Lexer.TokenType.Integer)
                        {
                            list.Add(new TagSpan<ClassificationTag>(new SnapshotSpan(shot, location), new ClassificationTag(Literal)));
                        }
                        if (lexer.IsKeyword(token.type))
                        {
                            list.Add(new TagSpan<ClassificationTag>(new SnapshotSpan(shot, location), new ClassificationTag(Keyword)));
                        }
                        return false;
                    }
                );
            }

            public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
        }
    }
}
