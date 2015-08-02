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
            return new LexerErrorTagger(LexerProvider.GetProviderForBuffer(buffer)) as ITagger<T>;
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

        ErrorTag ErrorFor(LexerProvider.Failure f)
        {
            switch(f)
            {
                case LexerProvider.Failure.UnlexableCharacter:
                    return new ErrorTag(PredefinedErrorTypeNames.SyntaxError, "The Wide lexer could not recognize this character");
                case LexerProvider.Failure.UnterminatedComment:
                    return new ErrorTag(PredefinedErrorTypeNames.SyntaxError, "This comment is unterminated");
                case LexerProvider.Failure.UnterminatedStringLiteral:
                    return new ErrorTag(PredefinedErrorTypeNames.SyntaxError, "This string literal is unterminated");
            }
            return null;
        }

        SnapshotSpan AdjustSpan(SnapshotSpan span, LexerProvider.Failure failure)
        {
            if (failure == LexerProvider.Failure.UnlexableCharacter)
                return span;
            return new SnapshotSpan(span.Snapshot, new Span(span.Snapshot.Length - 1, 1));
        }
        public IEnumerable<ITagSpan<ErrorTag>> GetTags(NormalizedSnapshotSpanCollection spans) 
        {
            var shot = spans[0].Snapshot;
            return provider.GetErrors(shot)
                           .SelectMany(error => spans
                                                    .Where(span => error.where.IntersectsWith(span))
                                                    .Select(span => new TagSpan<ErrorTag>(AdjustSpan(error.where, error.what), ErrorFor(error.what))));
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }   

}
