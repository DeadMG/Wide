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
using Microsoft.VisualStudio.Text.Adornments;
using Microsoft.VisualStudio.Utilities;
using System.Runtime.InteropServices;

namespace VisualWide.SourceHighlighting
{
    [Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ErrorTag))]
    internal class LexerErrorProviderProvider : ITaggerProvider
    {
        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new LexerErrorTagger(buffer.Properties.GetOrCreateSingletonProperty(typeof(LexerProvider), () => new LexerProvider(buffer))) as ITagger<T>;
        }
    }
    
    internal class LexerErrorTagger : ITagger<ErrorTag> 
    {
        LexerProvider provider;

        public LexerErrorTagger(LexerProvider lp) {
            provider = lp;
            provider.TagsChanged += (span) => {
                TagsChanged(this, new SnapshotSpanEventArgs(span));
            };
        }

        public IEnumerable<ITagSpan<ErrorTag>> GetTags(NormalizedSnapshotSpanCollection spans) 
        {
            var shot = spans[0].Snapshot;
            foreach (var error in provider.GetErrors(shot))
            {
                foreach (var span in spans)
                {
                    if (error.where.IntersectsWith(span))
                    {
                        if (error.what == Lexer.Failure.UnlexableCharacter)
                        {
                            yield return new TagSpan<ErrorTag>(error.where, new ErrorTag("syntax error", "The Wide lexer could not recognize this character."));
                        }
                        if (error.what == Lexer.Failure.UnterminatedStringLiteral)
                        {
                            yield return new TagSpan<ErrorTag>(new SnapshotSpan(shot, new Span(shot.Length - 1, 1)), new ErrorTag("syntax error", "This string is unterminated."));
                        }
                        if (error.what == Lexer.Failure.UnterminatedComment)
                        {
                            yield return new TagSpan<ErrorTag>(new SnapshotSpan(shot, new Span(shot.Length - 1, 1)), new ErrorTag("syntax error", "This comment is unterminated."));
                        }
                    }
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }   

}
