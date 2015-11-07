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
        internal const string ErrorType = "WideWarning";

        [Export(typeof(ErrorTypeDefinition))]
        [Name(ErrorType)]
        [DisplayName(ErrorType)]
        internal static ErrorTypeDefinition WideWarningError = null;

        [Export(typeof(EditorFormatDefinition))]
        [Name(ErrorType)]
        [Order(After = Priority.High)]
        [UserVisible(true)]
        internal class WideWarning : EditorFormatDefinition
        {
            public WideWarning()
            {
                this.DisplayName = "WideWarning"; //human readable version of the name
                this.ForegroundColor = Color.FromArgb(0xFF, 0, 0xFF, 0);
            }
        }

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
            if (shot != provider.GetTextBuffer().CurrentSnapshot)
                yield break;
            foreach (var error in provider.Errors)
            {
                foreach (var span in spans)
                {
                    if (error.where.IntersectsWith(span))
                    {
                        yield return new TagSpan<ErrorTag>(error.where, new ErrorTag(PredefinedErrorTypeNames.SyntaxError, error.what));
                    }
                }
            }
            foreach (var warning in provider.Warnings)
            {
                foreach (var span in spans)
                {
                    if (warning.where.IntersectsWith(span))
                    {
                        yield return new TagSpan<ErrorTag>(warning.where, new ErrorTag(ErrorType, ParserProvider.GetWarningString(warning.what)));
                    }
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }   
}
