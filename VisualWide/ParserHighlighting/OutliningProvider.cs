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
    [TagType(typeof(IOutliningRegionTag))]
    internal class OutliningProvider : ITaggerProvider
    {
        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new OutliningTagger(ParserProvider.GetProviderForBuffer(buffer)) as ITagger<T>;
        }
    }

    internal class OutliningTagger : ITagger<IOutliningRegionTag> 
    {
        private ParserProvider parser;

        public OutliningTagger(ParserProvider pp)
        {
            parser = pp;
            pp.TagsChanged += (span) =>
            {
                TagsChanged(this, new SnapshotSpanEventArgs(span));
            };
        }
        public IEnumerable<ITagSpan<IOutliningRegionTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            foreach (var outline in parser.GetOutline(spans[0].Snapshot))
            {
                foreach (var span in spans)
                {
                    if (outline.where.IntersectsWith(span))
                    {
                        var tag = new OutliningRegionTag(false, false, "{ ... }", "{ ... }");
                        yield return new TagSpan<OutliningRegionTag>(new SnapshotSpan(outline.where.Snapshot, outline.where.Start, outline.where.Length + 1), tag);
                    }
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }
}
