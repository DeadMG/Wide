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

        private string NameForDecltype(ParserProvider.DeclType type)
        {
            switch (type)
            {
                case ParserProvider.DeclType.Function:
                    return "function";
                case ParserProvider.DeclType.Module:
                    return "module";
                case ParserProvider.DeclType.Type:
                    return "type";
                case ParserProvider.DeclType.Using:
                    return "using";
            }
            return "unknown";
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
                        yield return new TagSpan<ErrorTag>(error.where, new ErrorTag(Microsoft.VisualStudio.Text.Adornments.PredefinedErrorTypeNames.SyntaxError, ParserProvider.GetErrorString(error.what)));
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
            foreach (var clash in provider.CombinedErrors.Concat(ProjectUtils.instance.GetProjectsFor(shot.TextBuffer).Select(proj => proj.CombinerErrors).Aggregate((x, y) => x.Concat(y))))
            {
                foreach (var loc in clash)
                {
                    string error = null;
                    foreach (var span in spans)
                    {
                        if (loc.where.Snapshot == shot && loc.where.IntersectsWith(span))
                        {
                            if (error == null)
                            {
                                error = "Error: This " + NameForDecltype(loc.what) + " clashes with";
                                foreach (var other in clash.Where(err => err.where.Snapshot != shot || !err.where.Equals(loc.where)))
                                {
                                    error += "\n    " + NameForDecltype(other.what) + " at file " + ProjectUtils.instance.GetFileName(other.where.Snapshot.TextBuffer) + " at line " + other.position.begin.line + ".";
                                }
                            }
                            yield return new TagSpan<ErrorTag>(loc.where, new ErrorTag(Microsoft.VisualStudio.Text.Adornments.PredefinedErrorTypeNames.SyntaxError, error));
                        }
                    }
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }   
}
