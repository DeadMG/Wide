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
    //[Export(typeof(ITaggerProvider))]
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
        TagSpan<OutliningRegionTag> CreateTag(ParserProvider.Outline outline)
        {
            var tag = new OutliningRegionTag(false, false, "{ ... }", "{ ... }");
            return new TagSpan<OutliningRegionTag>(new SnapshotSpan(outline.where.Snapshot, outline.where.Start, outline.where.Length + 1), tag);
        }
        public IEnumerable<ITagSpan<IOutliningRegionTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            return parser.Outlines.SelectMany(outline => spans.Where(span => outline.where.IntersectsWith(span)).Select(span => CreateTag(outline)));
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }
}
