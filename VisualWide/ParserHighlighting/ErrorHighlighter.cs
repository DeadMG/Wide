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

namespace VisualWide.ParserHighlighting
{
    [Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ErrorTag))]
    internal class ParserErrorProviderProvider : ITaggerProvider
    {
        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new ParserErrorTagger(ParserProvider.GetProviderForBuffer(buffer)) as ITagger<T>;
        }
    }

    internal class ParserErrorTagger : ITagger<ErrorTag>
    {
        ParserProvider provider;

        public ParserErrorTagger(ParserProvider lp)
        {
            provider = lp;
            provider.TagsChanged += (span) =>
            {
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
                        yield return new TagSpan<ErrorTag>(error.where, new ErrorTag("syntax error", ParserProvider.GetErrorString(error.what)));
                    }
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }   
}
