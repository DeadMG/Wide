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
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Shell;
using System.Runtime.InteropServices;

namespace VisualWide.AnalyzerHighlighting
{
    [Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ErrorTag))]
    internal class AnalyzerErrorProviderProvider : ITaggerProvider
    {
        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new AnalyzerErrorTagger(buffer) as ITagger<T>;
        }
    }
    internal class AnalyzerErrorTagger : ITagger<ErrorTag>
    {
        IEnumerable<ProjectUtils.Project.AnalyzerError> errors;
        ITextBuffer buffer;

        void UpdateErrors()
        {
            var list = ProjectUtils.instance.GetProjectsFor(buffer);
            if (list.Count() == 0) errors = new List<ProjectUtils.Project.AnalyzerError>();
            errors = list.Select(proj => proj.AnalyzerErrors.Where(err => err.where.Snapshot == buffer.CurrentSnapshot)).Aggregate((x, y) => x.Concat(y)).ToList();
            var span = new SnapshotSpan(buffer.CurrentSnapshot, new Span(0, buffer.CurrentSnapshot.Length));
            TagsChanged(this, new SnapshotSpanEventArgs(span));
        }

        public AnalyzerErrorTagger(ITextBuffer buf)
        {
            buffer = buf;
            UpdateErrors();
            foreach (var proj in ProjectUtils.instance.GetProjectsFor(buffer))
            {
                proj.OnUpdate += () => {
                    UpdateErrors();
                };
            }
        }

        public IEnumerable<ITagSpan<ErrorTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            return errors.Select(error => new TagSpan<ErrorTag>(error.where, new ErrorTag(Microsoft.VisualStudio.Text.Adornments.PredefinedErrorTypeNames.SyntaxError, error.error)));
        }
        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }
}
